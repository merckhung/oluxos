/*
 * tmpfs: RAM filesystem used for the root (populated from the initramfs),
 * /tmp and /dev. File data lives in individually allocated pages; the
 * filesystem size is capped (default: half of RAM).
 */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>

struct tmpfs_sb {
  u64 max_pages;
  u64 used_pages;
};

struct tmpfs_dirent {
  char *name;
  struct inode *inode;
  struct list_head link;
};

struct tmpfs_node {
  /* directory */
  struct list_head entries;
  struct inode *parent;
  /* regular file */
  struct page **pages;
  u64 npages; /* slots in pages[] */
  /* symlink */
  char *target;
};

static const struct inode_operations tmpfs_dir_iops;
static const struct inode_operations tmpfs_file_iops;
static const struct file_operations tmpfs_dir_fops;
static const struct file_operations tmpfs_file_fops;
static const struct inode_operations tmpfs_symlink_iops;

static struct tmpfs_node *node(struct inode *i) { return i->priv; }
static struct tmpfs_sb *tsb(struct super_block *sb) { return sb->priv; }

static struct inode *tmpfs_new_inode(struct super_block *sb, mode_t mode, dev_t rdev) {
  struct inode *i = new_inode(sb, mode);
  if (!i) return NULL;
  struct tmpfs_node *n = kzalloc(sizeof(*n), 0);
  if (!n) {
    inode_put(i);
    return NULL;
  }
  list_init(&n->entries);
  i->priv = n;
  i->rdev = rdev;
  if (S_ISDIR(mode)) {
    i->i_op = &tmpfs_dir_iops;
    i->f_op = &tmpfs_dir_fops;
    i->nlink = 2;
  } else if (S_ISLNK(mode)) {
    i->i_op = &tmpfs_symlink_iops;
  } else {
    i->i_op = &tmpfs_file_iops;
    if (S_ISREG(mode)) i->f_op = &tmpfs_file_fops;
  }
  return i;
}

static void truncate_pages(struct inode *i, loff_t size) {
  struct tmpfs_node *n = node(i);
  u64 keep = DIV_ROUND_UP((u64)size, PAGE_SIZE);
  for (u64 p = keep; p < n->npages; p++) {
    if (n->pages[p]) {
      free_page(n->pages[p]);
      n->pages[p] = NULL;
      tsb(i->sb)->used_pages--;
    }
  }
  /* zero the tail of the last partial page */
  if (size % PAGE_SIZE && keep && keep <= n->npages && n->pages[keep - 1])
    memset((u8 *)page_address(n->pages[keep - 1]) + size % PAGE_SIZE, 0, PAGE_SIZE - size % PAGE_SIZE);
  i->size = size;
  i->blocks = 0;
  for (u64 p = 0; p < MIN(keep, n->npages); p++)
    if (n->pages[p]) i->blocks += PAGE_SIZE / 512;
}

static void tmpfs_evict(struct inode *i) {
  struct tmpfs_node *n = node(i);
  if (!n) return;
  if (S_ISREG(i->mode)) {
    truncate_pages(i, 0);
    kfree(n->pages);
  }
  if (S_ISDIR(i->mode)) {
    struct tmpfs_dirent *e, *t;
    list_for_each_entry_safe(e, t, &n->entries, link) {
      list_del(&e->link);
      inode_put(e->inode);
      kfree(e->name);
      kfree(e);
    }
  }
  kfree(n->target);
  kfree(n);
  i->priv = NULL;
}

static struct tmpfs_dirent *find_entry(struct inode *dir, const char *name) {
  struct tmpfs_dirent *e;
  list_for_each_entry(e, &node(dir)->entries, link)
    if (!strcmp(e->name, name)) return e;
  return NULL;
}

static int add_entry(struct inode *dir, const char *name, struct inode *i) {
  if (strlen(name) > NAME_MAX) return -ENAMETOOLONG;
  struct tmpfs_dirent *e = kmalloc(sizeof(*e), 0);
  if (!e) return -ENOMEM;
  e->name = kstrdup(name, 0);
  if (!e->name) {
    kfree(e);
    return -ENOMEM;
  }
  e->inode = i;
  inode_get(i);
  list_add_tail(&e->link, &node(dir)->entries);
  dir->mtime = dir->ctime = current_time();
  dir->size++;
  return 0;
}

static void remove_entry(struct inode *dir, struct tmpfs_dirent *e) {
  list_del(&e->link);
  inode_put(e->inode);
  kfree(e->name);
  kfree(e);
  dir->mtime = dir->ctime = current_time();
  dir->size--;
}

static int tmpfs_lookup(struct inode *dir, const char *name, struct inode **out) {
  struct tmpfs_dirent *e = find_entry(dir, name);
  if (!e) return -ENOENT;
  inode_get(e->inode);
  *out = e->inode;
  return 0;
}

static int tmpfs_create(struct inode *dir, const char *name, mode_t mode, dev_t rdev, struct inode **out) {
  if (find_entry(dir, name)) return -EEXIST;
  struct inode *i = tmpfs_new_inode(dir->sb, mode, rdev);
  if (!i) return -ENOMEM;
  int r = add_entry(dir, name, i);
  if (r) {
    i->nlink = 0;
    inode_put(i);
    return r;
  }
  *out = i;
  return 0;
}

static int tmpfs_mkdir(struct inode *dir, const char *name, mode_t mode, struct inode **out) {
  int r = tmpfs_create(dir, name, S_IFDIR | (mode & 07777), 0, out);
  if (!r) {
    node(*out)->parent = dir;
    dir->nlink++;
  }
  return r;
}

static int tmpfs_unlink(struct inode *dir, const char *name, struct inode *victim) {
  struct tmpfs_dirent *e = find_entry(dir, name);
  if (!e) return -ENOENT;
  victim->nlink--;
  victim->ctime = current_time();
  remove_entry(dir, e);
  return 0;
}

static int tmpfs_rmdir(struct inode *dir, const char *name, struct inode *victim) {
  if (!list_empty(&node(victim)->entries)) return -ENOTEMPTY;
  struct tmpfs_dirent *e = find_entry(dir, name);
  if (!e) return -ENOENT;
  victim->nlink = 0;
  dir->nlink--;
  remove_entry(dir, e);
  return 0;
}

static int tmpfs_rename(struct inode *odir, const char *oname, struct inode *ndir, const char *nname,
                        struct inode *victim, struct inode *target) {
  struct tmpfs_dirent *oe = find_entry(odir, oname);
  if (!oe) return -ENOENT;
  if (target) {
    if (S_ISDIR(target->mode) && !list_empty(&node(target)->entries)) return -ENOTEMPTY;
    struct tmpfs_dirent *ne = find_entry(ndir, nname);
    if (ne) {
      if (S_ISDIR(target->mode)) {
        target->nlink = 0;
        ndir->nlink--;
      } else {
        target->nlink--;
      }
      remove_entry(ndir, ne);
    }
  }
  int r = add_entry(ndir, nname, victim);
  if (r) return r;
  if (S_ISDIR(victim->mode) && odir != ndir) {
    odir->nlink--;
    ndir->nlink++;
    node(victim)->parent = ndir;
  }
  remove_entry(odir, oe);
  victim->ctime = current_time();
  return 0;
}

static int tmpfs_link(struct inode *dir, const char *name, struct inode *target) {
  int r = add_entry(dir, name, target);
  if (!r) {
    target->nlink++;
    target->ctime = current_time();
  }
  return r;
}

static int tmpfs_symlink(struct inode *dir, const char *name, const char *tgt, struct inode **out) {
  int r = tmpfs_create(dir, name, S_IFLNK | 0777, 0, out);
  if (r) return r;
  node(*out)->target = kstrdup(tgt, 0);
  (*out)->size = strlen(tgt);
  return node(*out)->target ? 0 : -ENOMEM;
}

static ssize_t tmpfs_readlink(struct inode *i, char *buf, size_t size) {
  const char *t = node(i)->target;
  if (!t) return -EINVAL;
  size_t n = MIN(strlen(t), size);
  memcpy(buf, t, n);
  return n;
}

static int ensure_slots(struct tmpfs_node *n, u64 idx) {
  if (idx < n->npages) return 0;
  u64 nn = MAX(idx + 1, n->npages * 2);
  if (nn < 8) nn = 8;
  struct page **np = kcalloc(nn, sizeof(*np), 0);
  if (!np) return -ENOMEM;
  if (n->pages) memcpy(np, n->pages, n->npages * sizeof(*np));
  kfree(n->pages);
  n->pages = np;
  n->npages = nn;
  return 0;
}

static int tmpfs_setattr(struct inode *i, const struct iattr *a) {
  if (a->valid & ATTR_SIZE) {
    if (!S_ISREG(i->mode)) return -EINVAL;
    if (a->size < i->size) truncate_pages(i, a->size);
    else i->size = a->size; /* sparse extension */
  }
  if (a->valid & ATTR_MODE) i->mode = (i->mode & S_IFMT) | (a->mode & 07777);
  if (a->valid & ATTR_UID) i->uid = a->uid;
  if (a->valid & ATTR_GID) i->gid = a->gid;
  if (a->valid & ATTR_ATIME) i->atime = a->atime;
  if (a->valid & ATTR_MTIME) i->mtime = a->mtime;
  i->ctime = current_time();
  return 0;
}

static ssize_t tmpfs_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct inode *i = f->inode;
  struct tmpfs_node *n = node(i);
  if (*pos >= i->size) return 0;
  size_t total = MIN(b->len, (size_t)(i->size - *pos));
  size_t done = 0;
  static const u8 zero_page[PAGE_SIZE] __aligned(PAGE_SIZE);
  while (done < total) {
    u64 off = *pos + done;
    u64 idx = off / PAGE_SIZE, po = off % PAGE_SIZE;
    size_t chunk = MIN(total - done, PAGE_SIZE - po);
    const u8 *src = (idx < n->npages && n->pages[idx]) ? (u8 *)page_address(n->pages[idx]) : zero_page;
    if (iob_write(b, done, src + po, chunk)) return done ? (ssize_t)done : -EFAULT;
    done += chunk;
  }
  *pos += done;
  return done;
}

static ssize_t tmpfs_write(struct file *f, struct iobuf *b, loff_t *pos) {
  struct inode *i = f->inode;
  struct tmpfs_node *n = node(i);
  struct tmpfs_sb *s = tsb(i->sb);
  if (f->flags & O_APPEND) *pos = i->size;
  size_t done = 0;
  while (done < b->len) {
    u64 off = *pos + done;
    u64 idx = off / PAGE_SIZE, po = off % PAGE_SIZE;
    size_t chunk = MIN(b->len - done, PAGE_SIZE - po);
    if (ensure_slots(n, idx)) break;
    if (!n->pages[idx]) {
      if (s->used_pages >= s->max_pages) {
        if (!done) return -ENOSPC;
        break;
      }
      n->pages[idx] = alloc_page(GFP_ZERO);
      if (!n->pages[idx]) {
        if (!done) return -ENOSPC;
        break;
      }
      s->used_pages++;
      i->blocks += PAGE_SIZE / 512;
    }
    if (iob_read(b, done, (u8 *)page_address(n->pages[idx]) + po, chunk)) {
      if (!done) return -EFAULT;
      break;
    }
    done += chunk;
  }
  *pos += done;
  if (*pos > i->size) i->size = *pos;
  i->mtime = i->ctime = current_time();
  return done;
}

static loff_t generic_llseek(struct file *f, loff_t off, int whence) {
  loff_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos : whence == SEEK_END ? f->inode->size : -1;
  if (base < 0) return -EINVAL;
  loff_t np = base + off;
  if (np < 0) return -EINVAL;
  f->pos = np;
  return np;
}

static int tmpfs_iterate(struct file *f, struct dir_context *ctx) {
  struct inode *dir = f->inode;
  struct tmpfs_node *n = node(dir);
  if (ctx->pos == 0) {
    if (!ctx->actor(ctx, ".", 1, dir->ino, DT_DIR)) return 0;
    ctx->pos = 1;
  }
  if (ctx->pos == 1) {
    struct inode *p = n->parent ? n->parent : dir;
    if (!ctx->actor(ctx, "..", 2, p->ino, DT_DIR)) return 0;
    ctx->pos = 2;
  }
  loff_t idx = 2;
  struct tmpfs_dirent *e;
  list_for_each_entry(e, &n->entries, link) {
    if (idx++ < ctx->pos) continue;
    if (!ctx->actor(ctx, e->name, strlen(e->name), e->inode->ino, mode_to_dtype(e->inode->mode))) return 0;
    ctx->pos = idx;
  }
  return 0;
}

static int tmpfs_statfs(struct super_block *sb, u64 *blocks, u64 *bfree, u64 *files, u64 *ffree, u32 *bsize) {
  struct tmpfs_sb *s = tsb(sb);
  *bsize = PAGE_SIZE;
  *blocks = s->max_pages;
  *bfree = s->max_pages - s->used_pages;
  *files = s->max_pages;
  *ffree = s->max_pages - s->used_pages;
  return 0;
}

static const struct inode_operations tmpfs_dir_iops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir,
    .unlink = tmpfs_unlink,
    .rmdir = tmpfs_rmdir,
    .rename = tmpfs_rename,
    .link = tmpfs_link,
    .symlink = tmpfs_symlink,
    .setattr = tmpfs_setattr,
};
static const struct inode_operations tmpfs_file_iops = {.setattr = tmpfs_setattr};
static const struct inode_operations tmpfs_symlink_iops = {.readlink = tmpfs_readlink, .setattr = tmpfs_setattr};
static const struct file_operations tmpfs_file_fops = {
    .read = tmpfs_read, .write = tmpfs_write, .llseek = generic_llseek};
static const struct file_operations tmpfs_dir_fops = {.iterate = tmpfs_iterate, .llseek = generic_llseek};
static const struct super_operations tmpfs_sops = {.evict = tmpfs_evict, .statfs = tmpfs_statfs};

static int tmpfs_mount(struct super_block *sb, const char *dev, const char *data) {
  struct tmpfs_sb *s = kzalloc(sizeof(*s), 0);
  if (!s) return -ENOMEM;
  s->max_pages = nr_total_pages() / 2;
  if (data) {
    const char *sz = strstr(data, "size=");
    if (sz) {
      char *end;
      u64 v = strtoul(sz + 5, &end, 10);
      if (*end == 'k' || *end == 'K') v <<= 10;
      else if (*end == 'm' || *end == 'M') v <<= 20;
      else if (*end == 'g' || *end == 'G') v <<= 30;
      if (v) s->max_pages = DIV_ROUND_UP(v, PAGE_SIZE);
    }
  }
  sb->priv = s;
  sb->s_op = &tmpfs_sops;
  struct inode *root = tmpfs_new_inode(sb, S_IFDIR | 0755, 0);
  if (!root) return -ENOMEM;
  if (data && strstr(data, "mode=1777")) root->mode = S_IFDIR | 01777;
  sb->root = d_alloc(NULL, "/", root);
  return sb->root ? 0 : -ENOMEM;
}

static struct fs_type tmpfs_type = {.name = "tmpfs", .mount = tmpfs_mount, .nodev = true};
static struct fs_type ramfs_type = {.name = "ramfs", .mount = tmpfs_mount, .nodev = true};
static struct fs_type devtmpfs_type = {.name = "devtmpfs", .mount = tmpfs_mount, .nodev = true};

void tmpfs_register(void);
void tmpfs_register(void) {
  register_filesystem(&tmpfs_type);
  register_filesystem(&ramfs_type);
  register_filesystem(&devtmpfs_type);
}
