/*
 * userfs: filesystems implemented by userspace servers.
 *
 * Each VFS operation is marshalled into a `struct ufs_req`, sent over the
 * mount's IPC channel, and the caller sleeps until the matching reply
 * arrives. Requests on one mount are serialised. If the server dies, all
 * operations fail with -EIO until a restarted server re-attaches (mount
 * option "attach"); inode numbers are stable, so open files keep working.
 */
#include <olux/channel.h>
#include <olux/device.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/uaccess.h>
#include <uapi/olux/userfs.h>

#define UFS_TIMEOUT_NS ((long)(30 * NSEC_PER_SEC))
#define UFS_HASH 256
#define REQ_SIZE (sizeof(struct ufs_req) + UFS_MAX_IO)
#define REP_SIZE (sizeof(struct ufs_reply) + UFS_MAX_IO)

struct ufs_sb {
  struct file *chan; /* kernel end of the channel */
  struct mutex lock;
  u32 next_id;
  u8 *reqbuf, *repbuf;
  struct list_head hash[UFS_HASH];
  bool dead;
  char source[64];
};

struct ufs_inode {
  u64 ino;
  struct inode *inode;
  struct list_head hash;
};

static const struct inode_operations ufs_dir_iops, ufs_file_iops, ufs_link_iops;
static const struct file_operations ufs_dir_fops, ufs_file_fops;

static struct ufs_sb *usb(struct super_block *sb) { return sb->priv; }
static u64 uino(struct inode *i) { return ((struct ufs_inode *)i->priv)->ino; }

/* Send the request in s->reqbuf (+dlen bytes of data already placed after
 * it) and wait for the matching reply. Caller holds s->lock. */
static int ufs_call(struct super_block *sb, size_t dlen, struct ufs_reply **rep_out) {
  struct ufs_sb *s = usb(sb);
  if (!s->chan || s->dead) return -EIO;
  struct ufs_req *req = (struct ufs_req *)s->reqbuf;
  req->id = ++s->next_id;
  int r = chan_send_kernel(s->chan, s->reqbuf, sizeof(*req) + dlen);
  if (r) goto dead;
  for (;;) {
    s32 pid;
    ssize_t n = chan_recv_kernel(s->chan, s->repbuf, REP_SIZE, UFS_TIMEOUT_NS, &pid);
    if (n < 0) {
      r = (int)n;
      goto dead;
    }
    struct ufs_reply *rep = (struct ufs_reply *)s->repbuf;
    if ((size_t)n < sizeof(*rep) || rep->len > (size_t)n - sizeof(*rep)) {
      r = -EPROTO;
      goto dead;
    }
    if (rep->id != req->id) continue; /* stale reply of a timed-out request */
    *rep_out = rep;
    if (rep->err > 0 || rep->err < -MAX_ERRNO) return -EIO;
    return rep->err;
  }
dead:
  if (!s->dead) pr_err("userfs: server for %s stopped responding (%d); I/O fails until it restarts\n", s->source, r);
  s->dead = true;
  return -EIO;
}

static void apply_attr(struct inode *i, const struct ufs_attr *a) {
  i->mode = a->mode;
  i->nlink = a->nlink;
  i->uid = a->uid;
  i->gid = a->gid;
  i->size = a->size;
  i->blocks = a->blocks;
  i->rdev = a->rdev;
  i->atime = (struct timespec64){a->atime, 0};
  i->mtime = (struct timespec64){a->mtime, 0};
  i->ctime = (struct timespec64){a->ctime, 0};
}

static struct inode *ufs_iget(struct super_block *sb, const struct ufs_attr *a) {
  struct ufs_sb *s = usb(sb);
  struct list_head *h = &s->hash[a->ino % UFS_HASH];
  struct ufs_inode *ui;
  list_for_each_entry(ui, h, hash) {
    if (ui->ino == a->ino) {
      inode_get(ui->inode);
      apply_attr(ui->inode, a);
      return ui->inode;
    }
  }
  struct inode *i = new_inode(sb, a->mode);
  ui = kzalloc(sizeof(*ui), 0);
  if (!i || !ui) {
    if (i) inode_put(i);
    kfree(ui);
    return NULL;
  }
  ui->ino = a->ino;
  ui->inode = i;
  list_add(&ui->hash, h);
  i->priv = ui;
  i->ino = a->ino;
  if (S_ISDIR(a->mode)) {
    i->i_op = &ufs_dir_iops;
    i->f_op = &ufs_dir_fops;
  } else if (S_ISLNK(a->mode)) {
    i->i_op = &ufs_link_iops;
  } else {
    i->i_op = &ufs_file_iops;
    if (S_ISREG(a->mode)) i->f_op = &ufs_file_fops;
  }
  apply_attr(i, a);
  return i;
}

static void ufs_evict(struct inode *i) {
  struct ufs_inode *ui = i->priv;
  if (ui) {
    list_del(&ui->hash);
    kfree(ui);
  }
}

static struct ufs_req *begin(struct super_block *sb, u32 op, struct inode *i) {
  mutex_lock(&usb(sb)->lock);
  struct ufs_req *r = (struct ufs_req *)usb(sb)->reqbuf;
  memset(r, 0, sizeof(*r));
  r->op = op;
  r->ino = i ? uino(i) : 0;
  if (current->proc) {
    r->uid = current->proc->cred.euid;
    r->gid = current->proc->cred.egid;
  }
  return r;
}

static void end(struct super_block *sb) { mutex_unlock(&usb(sb)->lock); }

static int set_name(char *dst, size_t size, const char *src) {
  return strlcpy(dst, src, size) >= size ? -ENAMETOOLONG : 0;
}

/* ---------------- inode operations ---------------- */

static int ufs_lookup(struct inode *dir, const char *name, struct inode **out) {
  struct super_block *sb = dir->sb;
  struct ufs_req *q = begin(sb, UFS_LOOKUP, dir);
  struct ufs_reply *rep;
  int r = set_name(q->name, sizeof(q->name), name);
  if (!r) r = ufs_call(sb, 0, &rep);
  if (!r) {
    *out = ufs_iget(sb, &rep->attr);
    if (!*out) r = -ENOMEM;
  }
  end(sb);
  return r;
}

static int make(struct inode *dir, u32 op, const char *name, mode_t mode, dev_t rdev, const char *target,
                struct inode **out) {
  struct super_block *sb = dir->sb;
  struct ufs_req *q = begin(sb, op, dir);
  struct ufs_reply *rep;
  int r = set_name(q->name, sizeof(q->name), name);
  if (!r && target) r = set_name(q->name2, sizeof(q->name2), target);
  q->mode = mode;
  q->rdev = rdev;
  if (!r) r = ufs_call(sb, 0, &rep);
  if (!r) {
    *out = ufs_iget(sb, &rep->attr);
    if (!*out) r = -ENOMEM;
    dir->mtime = dir->ctime = current_time();
  }
  end(sb);
  return r;
}

static int ufs_create(struct inode *dir, const char *name, mode_t mode, dev_t rdev, struct inode **out) {
  return make(dir, UFS_CREATE, name, mode, rdev, NULL, out);
}

static int ufs_mkdir(struct inode *dir, const char *name, mode_t mode, struct inode **out) {
  int r = make(dir, UFS_MKDIR, name, mode, 0, NULL, out);
  if (!r) dir->nlink++;
  return r;
}

static int ufs_symlink(struct inode *dir, const char *name, const char *target, struct inode **out) {
  return make(dir, UFS_SYMLINK, name, S_IFLNK | 0777, 0, target, out);
}

static int simple(struct inode *dir, u32 op, const char *name, struct inode *ino2, const char *name2) {
  struct super_block *sb = dir->sb;
  struct ufs_req *q = begin(sb, op, dir);
  struct ufs_reply *rep;
  int r = set_name(q->name, sizeof(q->name), name);
  if (!r && name2) r = set_name(q->name2, sizeof(q->name2), name2);
  if (ino2) q->ino2 = uino(ino2);
  if (!r) r = ufs_call(sb, 0, &rep);
  if (!r) dir->mtime = dir->ctime = current_time();
  end(sb);
  return r;
}

static int ufs_unlink(struct inode *dir, const char *name, struct inode *victim) {
  int r = simple(dir, UFS_UNLINK, name, NULL, NULL);
  if (!r && victim->nlink) victim->nlink--;
  return r;
}

static int ufs_rmdir(struct inode *dir, const char *name, struct inode *victim) {
  int r = simple(dir, UFS_RMDIR, name, NULL, NULL);
  if (!r) {
    victim->nlink = 0;
    if (dir->nlink > 2) dir->nlink--;
  }
  return r;
}

static int ufs_rename(struct inode *odir, const char *oname, struct inode *ndir, const char *nname,
                      struct inode *victim, struct inode *target) {
  int r = simple(odir, UFS_RENAME, oname, ndir, nname);
  if (!r && target) target->nlink = 0;
  return r;
}

static int ufs_link(struct inode *dir, const char *name, struct inode *target) {
  int r = simple(dir, UFS_LINK, name, target, NULL);
  if (!r) target->nlink++;
  return r;
}

static ssize_t ufs_readlink(struct inode *i, char *buf, size_t size) {
  struct super_block *sb = i->sb;
  begin(sb, UFS_READLINK, i);
  struct ufs_reply *rep;
  ssize_t r = ufs_call(sb, 0, &rep);
  if (!r) {
    r = MIN((size_t)rep->len, size);
    memcpy(buf, rep + 1, r);
  }
  end(sb);
  return r;
}

static int ufs_setattr(struct inode *i, const struct iattr *a) {
  struct super_block *sb = i->sb;
  struct ufs_req *q = begin(sb, UFS_SETATTR, i);
  q->valid = a->valid;
  q->mode = a->mode;
  q->size = a->size;
  q->atime = a->atime.tv_sec;
  q->mtime = a->mtime.tv_sec;
  if (a->valid & ATTR_UID) q->uid = a->uid;
  if (a->valid & ATTR_GID) q->gid = a->gid;
  struct ufs_reply *rep;
  int r = ufs_call(sb, 0, &rep);
  if (!r) apply_attr(i, &rep->attr);
  end(sb);
  return r;
}

/* ---------------- file operations ---------------- */

static ssize_t ufs_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct inode *i = f->inode;
  struct super_block *sb = i->sb;
  size_t done = 0;
  while (done < b->len) {
    size_t want = MIN(b->len - done, (size_t)UFS_MAX_IO);
    struct ufs_req *q = begin(sb, UFS_READ, i);
    q->off = *pos;
    q->len = want;
    struct ufs_reply *rep;
    int r = ufs_call(sb, 0, &rep);
    size_t got = 0;
    if (!r) {
      got = MIN((size_t)rep->len, want);
      apply_attr(i, &rep->attr);
      if (got && iob_write(b, done, rep + 1, got)) r = -EFAULT;
    }
    end(sb);
    if (r) return done ? (ssize_t)done : r;
    done += got;
    *pos += got;
    if (got < want) break;
  }
  return done;
}

static ssize_t ufs_write(struct file *f, struct iobuf *b, loff_t *pos) {
  struct inode *i = f->inode;
  struct super_block *sb = i->sb;
  if (f->flags & O_APPEND) *pos = i->size;
  size_t done = 0;
  while (done < b->len) {
    size_t n = MIN(b->len - done, (size_t)UFS_MAX_IO);
    struct ufs_req *q = begin(sb, UFS_WRITE, i);
    q->off = *pos;
    q->len = n;
    int r = iob_read(b, done, (u8 *)(q + 1), n);
    struct ufs_reply *rep;
    size_t wrote = 0;
    if (!r) r = ufs_call(sb, n, &rep);
    if (!r) {
      apply_attr(i, &rep->attr);
      wrote = MIN((size_t)rep->count, n);
    }
    end(sb);
    if (r) return done ? (ssize_t)done : r;
    done += wrote;
    *pos += wrote;
    if (wrote < n) {
      if (!done) return -ENOSPC;
      break;
    }
  }
  return done;
}

static int ufs_iterate(struct file *f, struct dir_context *ctx) {
  struct inode *i = f->inode;
  struct super_block *sb = i->sb;
  for (;;) {
    struct ufs_req *q = begin(sb, UFS_READDIR, i);
    q->off = ctx->pos;
    q->len = UFS_MAX_IO;
    struct ufs_reply *rep;
    int r = ufs_call(sb, 0, &rep);
    if (r) {
      end(sb);
      return r;
    }
    if (rep->len == 0) {
      end(sb);
      return 0;
    }
    /* copy out: the actor may fault and must not run under our lock with
     * a buffer that the next request would overwrite */
    size_t len = rep->len;
    u8 *copy = kmalloc(len, 0);
    if (copy) memcpy(copy, rep + 1, len);
    end(sb);
    if (!copy) return -ENOMEM;
    size_t off = 0;
    bool full = false;
    while (off + sizeof(struct ufs_dirent) <= len) {
      struct ufs_dirent *d = (struct ufs_dirent *)(copy + off);
      if (d->reclen < sizeof(*d) || off + d->reclen > len || sizeof(*d) + d->namelen > d->reclen) break;
      if (!ctx->actor(ctx, d->name, d->namelen, d->ino, d->type)) {
        full = true;
        break;
      }
      ctx->pos = (loff_t)d->next;
      off += d->reclen;
    }
    kfree(copy);
    if (full) return 0;
  }
}

static int ufs_fsync(struct file *f) {
  struct super_block *sb = f->inode->sb;
  begin(sb, UFS_SYNC, NULL);
  struct ufs_reply *rep;
  int r = ufs_call(sb, 0, &rep);
  end(sb);
  return r;
}

static loff_t ufs_llseek(struct file *f, loff_t off, int whence) {
  loff_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos : whence == SEEK_END ? f->inode->size : -1;
  if (base < 0 || base + off < 0) return -EINVAL;
  return f->pos = base + off;
}

/* ---------------- superblock ---------------- */

static int ufs_statfs(struct super_block *sb, u64 *blocks, u64 *bfree, u64 *files, u64 *ffree, u32 *bsize) {
  begin(sb, UFS_STATFS, NULL);
  struct ufs_reply *rep;
  int r = ufs_call(sb, 0, &rep);
  if (!r) {
    *blocks = rep->st.blocks;
    *bfree = rep->st.bfree;
    *files = rep->st.files;
    *ffree = rep->st.ffree;
    *bsize = rep->st.bsize ? rep->st.bsize : 512;
  }
  end(sb);
  return r;
}

static int ufs_sync(struct super_block *sb) {
  begin(sb, UFS_SYNC, NULL);
  struct ufs_reply *rep;
  int r = ufs_call(sb, 0, &rep);
  end(sb);
  return r;
}

static void ufs_put_super(struct super_block *sb) {
  struct ufs_sb *s = usb(sb);
  if (s->chan) file_put(s->chan);
  vfree(s->reqbuf);
  vfree(s->repbuf);
}

static const struct inode_operations ufs_dir_iops = {
    .lookup = ufs_lookup,
    .create = ufs_create,
    .mkdir = ufs_mkdir,
    .unlink = ufs_unlink,
    .rmdir = ufs_rmdir,
    .rename = ufs_rename,
    .link = ufs_link,
    .symlink = ufs_symlink,
    .setattr = ufs_setattr,
};
static const struct inode_operations ufs_file_iops = {.setattr = ufs_setattr};
static const struct inode_operations ufs_link_iops = {.readlink = ufs_readlink, .setattr = ufs_setattr};
static const struct file_operations ufs_file_fops = {
    .read = ufs_read, .write = ufs_write, .llseek = ufs_llseek, .fsync = ufs_fsync};
static const struct file_operations ufs_dir_fops = {.iterate = ufs_iterate, .llseek = ufs_llseek, .fsync = ufs_fsync};
static const struct super_operations ufs_sops = {
    .evict = ufs_evict, .statfs = ufs_statfs, .sync = ufs_sync, .put_super = ufs_put_super};

static int parse_fd(const char *data, bool *attach) {
  if (!data) return -EINVAL;
  const char *p = strstr(data, "fd=");
  if (!p) return -EINVAL;
  *attach = strstr(data, "attach") != NULL;
  return (int)strtol(p + 3, NULL, 10);
}

/* The server queues an unsolicited greeting (a UFS_HELLO reply with id 0
 * describing the root) before calling mount(): the mounting thread is the
 * server itself, so the kernel must not wait for it to answer a request. */
static int ufs_greeting(struct ufs_sb *s, struct ufs_attr *root) {
  s32 pid;
  ssize_t n = chan_recv_kernel(s->chan, s->repbuf, REP_SIZE, 1 /* no waiting */, &pid);
  if (n == -EAGAIN || n == -ETIMEDOUT) return -EPROTO; /* no greeting queued */
  if (n < 0) return (int)n;
  struct ufs_reply *rep = (struct ufs_reply *)s->repbuf;
  if ((size_t)n < sizeof(*rep) || rep->id != 0 || rep->err) return -EPROTO;
  *root = rep->attr;
  return 0;
}

/* Re-bind a mount whose server died to a new server's channel. */
static int ufs_attach(struct file *chan, const char *dir) {
  struct path p;
  int r = kern_path(dir, LOOKUP_FOLLOW, &p);
  if (r) return r;
  struct super_block *sb = p.mnt->sb;
  if (p.dentry != sb->root || strcmp(sb->type->name, "userfs")) {
    path_put(&p);
    return -EINVAL;
  }
  struct ufs_sb *s = usb(sb);
  mutex_lock(&s->lock);
  if (!s->dead && s->chan && !chan_peer_closed(s->chan)) {
    mutex_unlock(&s->lock);
    path_put(&p);
    return -EBUSY;
  }
  struct file *old = s->chan;
  s->chan = chan;
  s->dead = false;
  if (old) file_put(old);
  /* verify the new server serves the same root */
  struct ufs_attr ra;
  r = ufs_greeting(s, &ra);
  if (!r && ra.ino != uino(sb->root->inode)) r = -ESTALE;
  if (r) s->dead = true;
  mutex_unlock(&s->lock);
  if (!r) pr_info("userfs: server re-attached to %s\n", s->source);
  path_put(&p);
  return r;
}

static int ufs_mount(struct super_block *sb, const char *dev, const char *data) {
  bool attach;
  int fd = parse_fd(data, &attach);
  if (fd < 0) return -EINVAL;
  struct file *chan = fget(fd);
  if (!chan) return -EBADF;
  if (!is_channel(chan)) {
    file_put(chan);
    return -EINVAL;
  }
  struct ufs_sb *s = kzalloc(sizeof(*s), 0);
  if (!s) {
    file_put(chan);
    return -ENOMEM;
  }
  s->reqbuf = vmalloc(REQ_SIZE);
  s->repbuf = vmalloc(REP_SIZE);
  if (!s->reqbuf || !s->repbuf) {
    file_put(chan);
    return -ENOMEM;
  }
  s->chan = chan;
  mutex_init(&s->lock);
  for (int i = 0; i < UFS_HASH; i++) list_init(&s->hash[i]);
  strlcpy(s->source, dev ? dev : "userfs", sizeof(s->source));
  sb->priv = s;
  sb->s_op = &ufs_sops;
  struct ufs_attr ra;
  int r = ufs_greeting(s, &ra);
  struct inode *root = NULL;
  if (!r) {
    root = ufs_iget(sb, &ra);
    if (!root || !S_ISDIR(root->mode)) r = -ENOTDIR;
  }
  if (r) {
    if (root) inode_put(root);
    ufs_put_super(sb);
    kfree(s);
    return r;
  }
  sb->root = d_alloc(NULL, "/", root);
  return sb->root ? 0 : -ENOMEM;
}

static struct fs_type userfs_type = {.name = "userfs", .mount = ufs_mount};

/* Called by sys_mount for "userfs" with the attach option. */
int userfs_try_attach(const char *dir, const char *data);
int userfs_try_attach(const char *dir, const char *data) {
  bool attach;
  int fd = parse_fd(data, &attach);
  if (!attach) return 1; /* not an attach request */
  struct file *chan = fget(fd);
  if (!chan) return -EBADF;
  if (!is_channel(chan)) {
    file_put(chan);
    return -EINVAL;
  }
  int r = ufs_attach(chan, dir);
  if (r) file_put(chan);
  return r;
}

static int userfs_init(void) { return register_filesystem(&userfs_type); }
fs_initcall(userfs_init);
