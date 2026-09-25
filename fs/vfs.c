/*
 * Virtual filesystem core: inodes, dentry cache, mounts, path resolution,
 * open and namespace operations. Callers hold the big kernel lock.
 *
 * Dentries form a tree rooted at each superblock. A dentry holds a
 * reference on its parent and its inode; unused dentries are freed as soon
 * as their last reference goes away (filesystems own their directory data).
 */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/uaccess.h>

static struct fs_type *filesystems;
static LIST_HEAD(mounts);
static struct mount *root_mnt;

struct timespec64 current_time(void) {
  u64 ns = ktime_realtime_ns();
  return (struct timespec64){(s64)(ns / NSEC_PER_SEC), (s64)(ns % NSEC_PER_SEC)};
}

unsigned mode_to_dtype(mode_t m) { return (m & S_IFMT) >> 12; }

int register_filesystem(struct fs_type *fs) {
  fs->next = filesystems;
  filesystems = fs;
  return 0;
}

static struct fs_type *find_fs(const char *name) {
  for (struct fs_type *f = filesystems; f; f = f->next)
    if (!strcmp(f->name, name)) return f;
  return NULL;
}

/* ---------------- inodes ---------------- */

struct inode *new_inode(struct super_block *sb, mode_t mode) {
  struct inode *i = kzalloc(sizeof(*i), 0);
  if (!i) return NULL;
  i->sb = sb;
  i->mode = mode;
  i->ino = ++sb->next_ino;
  i->nlink = 1;
  i->uid = current && current->proc ? current->proc->cred.euid : 0;
  i->gid = current && current->proc ? current->proc->cred.egid : 0;
  i->atime = i->mtime = i->ctime = current_time();
  atomic_set(&i->refcount, 1);
  list_add(&i->sb_link, &sb->inodes);
  return i;
}

void inode_get(struct inode *i) { atomic_inc(&i->refcount); }

void inode_put(struct inode *i) {
  if (!i) return;
  if (atomic_dec_return(&i->refcount) > 0) return;
  list_del(&i->sb_link);
  if (i->sb && i->sb->s_op && i->sb->s_op->evict) i->sb->s_op->evict(i);
  kfree(i);
}

/* ---------------- dentries ---------------- */

struct dentry *d_alloc(struct dentry *parent, const char *name, struct inode *inode) {
  struct dentry *d = kzalloc(sizeof(*d), 0);
  if (!d) return NULL;
  d->name = kstrdup(name, 0);
  if (!d->name) {
    kfree(d);
    return NULL;
  }
  d->inode = inode; /* consumes the caller's inode reference */
  atomic_set(&d->refcount, 1);
  list_init(&d->children);
  list_init(&d->sibling);
  if (parent) {
    d->parent = parent;
    d_get(parent);
    list_add(&d->sibling, &parent->children);
  }
  return d;
}

void d_get(struct dentry *d) { atomic_inc(&d->refcount); }

void d_put(struct dentry *d) {
  while (d && atomic_dec_return(&d->refcount) == 0) {
    struct dentry *parent = d->parent;
    if (parent) list_del(&d->sibling);
    inode_put(d->inode);
    kfree(d->name);
    kfree(d);
    d = parent;
  }
}

static void d_drop(struct dentry *d) {
  /* unlinked: remove from the tree so lookups no longer find it */
  if (d->parent && list_linked(&d->sibling)) {
    list_del(&d->sibling);
    list_init(&d->sibling);
  }
  d->dead = true;
}

void path_get(struct path *p) {
  if (p->dentry) d_get(p->dentry);
}
void path_put(struct path *p) {
  if (p->dentry) d_put(p->dentry);
}

static struct dentry *d_cached(struct dentry *dir, const char *name) {
  struct dentry *c;
  list_for_each_entry(c, &dir->children, sibling) {
    if (!strcmp(c->name, name)) {
      d_get(c);
      return c;
    }
  }
  return NULL;
}

/* Find (or create) the child dentry `name` of dir. */
static int d_lookup(struct dentry *dir, const char *name, struct dentry **out) {
  struct dentry *c = d_cached(dir, name);
  if (c) {
    *out = c;
    return 0;
  }
  struct inode *di = dir->inode;
  if (!di->i_op || !di->i_op->lookup) return -ENOENT;
  struct inode *ci = NULL;
  int r = di->i_op->lookup(di, name, &ci);
  if (r) return r;
  c = d_alloc(dir, name, ci);
  if (!c) {
    inode_put(ci);
    return -ENOMEM;
  }
  /* d_alloc's initial reference is returned to the caller */
  *out = c;
  return 0;
}

/* ---------------- permissions ---------------- */

int permission(struct inode *inode, int mask) {
  struct process *p = current->proc;
  if (!p) return 0;
  mode_t m = inode->mode;
  if (p->cred.euid == 0) {
    if ((mask & 1) && !S_ISDIR(m) && !(m & 0111)) return -EACCES;
    return 0;
  }
  int bits;
  if (p->cred.euid == inode->uid) bits = (m >> 6) & 7;
  else {
    bool in_group = p->cred.egid == inode->gid;
    for (int i = 0; i < p->cred.ngroups && !in_group; i++) in_group = p->cred.groups[i] == inode->gid;
    bits = in_group ? (m >> 3) & 7 : m & 7;
  }
  return (bits & mask) == mask ? 0 : -EACCES;
}

/* ---------------- path resolution ---------------- */

static void follow_mounts(struct path *p) {
  while (p->dentry->mounted) {
    struct mount *m = p->dentry->mounted;
    struct dentry *root = m->sb->root;
    d_get(root);
    d_put(p->dentry);
    p->mnt = m;
    p->dentry = root;
  }
}

static void follow_dotdot(struct path *p, const struct path *root) {
  for (;;) {
    if (p->dentry == root->dentry && p->mnt == root->mnt) return;
    if (p->dentry != p->mnt->sb->root) {
      struct dentry *parent = p->dentry->parent;
      d_get(parent);
      d_put(p->dentry);
      p->dentry = parent;
      return;
    }
    if (!p->mnt->parent) return; /* global root */
    struct dentry *mp = p->mnt->mountpoint;
    d_get(mp);
    d_put(p->dentry);
    p->dentry = mp;
    p->mnt = p->mnt->parent;
  }
}

static int start_path(int dirfd, const char *name, struct path *out) {
  struct process *proc = current->proc;
  if (name[0] == '/') {
    *out = proc ? proc->root : (struct path){root_mnt, root_mnt->sb->root};
  } else if (dirfd == AT_FDCWD) {
    *out = proc ? proc->cwd : (struct path){root_mnt, root_mnt->sb->root};
  } else {
    struct file *f = fget(dirfd);
    if (!f) return -EBADF;
    if (!f->path.dentry || !S_ISDIR(f->inode->mode)) {
      file_put(f);
      return -ENOTDIR;
    }
    *out = f->path;
    path_get(out);
    file_put(f);
    return 0;
  }
  path_get(out);
  return 0;
}

/*
 * Core walker. Resolves `name` starting at *p (referenced; replaced by the
 * result). If `last` is non-NULL, stops before the final component and
 * copies it there (LOOKUP_PARENT semantics).
 */
static int walk(struct path *p, const char *name, unsigned flags, char *last, int *depth) {
  struct path root;
  struct process *proc = current->proc;
  root = proc ? proc->root : (struct path){root_mnt, root_mnt->sb->root};
  char comp[NAME_MAX + 1];

  if (*name == '/') {
    path_put(p);
    *p = root;
    path_get(p);
  }
  while (*name == '/') name++;
  if (last) last[0] = '\0';

  while (*name) {
    const char *end = name;
    while (*end && *end != '/') end++;
    size_t len = end - name;
    if (len > NAME_MAX) return -ENAMETOOLONG;
    memcpy(comp, name, len);
    comp[len] = '\0';
    const char *rest = end;
    while (*rest == '/') rest++;
    bool is_last = *rest == '\0';
    bool trailing_slash = *end == '/';

    if (!S_ISDIR(p->dentry->inode->mode)) return -ENOTDIR;
    if (last && is_last) {
      if (!strcmp(comp, ".") || !strcmp(comp, "..")) {
        /* treat as a lookup of the directory itself */
        if (!strcmp(comp, "..")) follow_dotdot(p, &root);
        strlcpy(last, ".", NAME_MAX + 1);
      } else {
        strlcpy(last, comp, NAME_MAX + 1);
      }
      return 0;
    }
    int r = permission(p->dentry->inode, 1);
    if (r) return r;

    if (!strcmp(comp, ".")) {
      name = rest;
      continue;
    }
    if (!strcmp(comp, "..")) {
      follow_dotdot(p, &root);
      name = rest;
      continue;
    }
    struct dentry *child;
    r = d_lookup(p->dentry, comp, &child);
    if (r) return r;
    struct path next = {p->mnt, child};
    follow_mounts(&next);

    struct inode *ci = next.dentry->inode;
    if (S_ISLNK(ci->mode) && (!is_last || (flags & LOOKUP_FOLLOW) || trailing_slash)) {
      if (++*depth > MAX_SYMLINKS) {
        path_put(&next);
        return -ELOOP;
      }
      char *target = kmalloc(PATH_MAX, 0);
      if (!target) {
        path_put(&next);
        return -ENOMEM;
      }
      ssize_t n = ci->i_op && ci->i_op->readlink ? ci->i_op->readlink(ci, target, PATH_MAX - 1) : -EINVAL;
      path_put(&next);
      if (n < 0) {
        kfree(target);
        return (int)n;
      }
      target[n] = '\0';
      /* resolve the link relative to the directory containing it */
      r = walk(p, target, LOOKUP_FOLLOW, NULL, depth);
      kfree(target);
      if (r) return r;
    } else {
      path_put(p);
      *p = next;
    }
    name = rest;
    if (is_last && trailing_slash && !S_ISDIR(p->dentry->inode->mode)) return -ENOTDIR;
  }
  if (last) strlcpy(last, ".", NAME_MAX + 1);
  if ((flags & LOOKUP_DIRECTORY) && !S_ISDIR(p->dentry->inode->mode)) return -ENOTDIR;
  return 0;
}

int path_lookupat(int dirfd, const char *name, unsigned flags, struct path *out) {
  if (!*name) return -ENOENT;
  struct path p;
  int r = start_path(dirfd, name, &p);
  if (r) return r;
  int depth = 0;
  r = walk(&p, name, flags, NULL, &depth);
  if (r) {
    path_put(&p);
    return r;
  }
  *out = p;
  return 0;
}

int kern_path(const char *name, unsigned flags, struct path *out) {
  return path_lookupat(AT_FDCWD, name, flags, out);
}

int path_parentat(int dirfd, const char *name, struct path *parent, char *last) {
  if (!*name) return -ENOENT;
  struct path p;
  int r = start_path(dirfd, name, &p);
  if (r) return r;
  int depth = 0;
  r = walk(&p, name, LOOKUP_FOLLOW, last, &depth);
  if (r) {
    path_put(&p);
    return r;
  }
  if (!S_ISDIR(p.dentry->inode->mode)) {
    path_put(&p);
    return -ENOTDIR;
  }
  *parent = p;
  return 0;
}

/* Build an absolute path string for p. Returns length or error. */
int d_path(const struct path *p, char *buf, size_t size) {
  char *end = buf + size;
  char *s = end;
  *--s = '\0';
  struct path cur = *p;
  struct process *proc = current->proc;
  struct path root = proc ? proc->root : (struct path){root_mnt, root_mnt->sb->root};
  for (;;) {
    if (cur.dentry == root.dentry && cur.mnt == root.mnt) break;
    if (cur.dentry == cur.mnt->sb->root) {
      if (!cur.mnt->parent) break;
      cur.dentry = cur.mnt->mountpoint;
      cur.mnt = cur.mnt->parent;
      continue;
    }
    size_t n = strlen(cur.dentry->name);
    if ((size_t)(s - buf) < n + 2) return -ENAMETOOLONG;
    s -= n;
    memcpy(s, cur.dentry->name, n);
    *--s = '/';
    cur.dentry = cur.dentry->parent;
  }
  if (*s == '\0') *--s = '/';
  size_t len = end - s - 1;
  memmove(buf, s, len + 1);
  return (int)len;
}

/* ---------------- namespace operations ---------------- */

static int lookup_last(struct path *parent, const char *last, struct dentry **out) {
  return d_lookup(parent->dentry, last, out);
}

static int may_create(struct path *parent) {
  if (parent->mnt->sb->flags & SB_RDONLY) return -EROFS;
  if (parent->dentry->dead) return -ENOENT;
  return permission(parent->dentry->inode, 3);
}

int vfs_mknod(int dirfd, const char *name, mode_t mode, dev_t dev) {
  struct path parent;
  char last[NAME_MAX + 1];
  int r = path_parentat(dirfd, name, &parent, last);
  if (r) return r;
  struct dentry *d;
  if (!strcmp(last, ".")) r = -EEXIST;
  else if (lookup_last(&parent, last, &d) == 0) {
    d_put(d);
    r = -EEXIST;
  } else if (!(r = may_create(&parent))) {
    struct inode *dir = parent.dentry->inode;
    struct inode *ni = NULL;
    if (!dir->i_op || !dir->i_op->create) r = -EPERM;
    else {
      mode_t um = current->proc ? current->proc->umask : 022;
      r = dir->i_op->create(dir, last, (mode & S_IFMT) | (mode & 07777 & ~um), dev, &ni);
      if (!r) inode_put(ni);
    }
  }
  path_put(&parent);
  return r;
}

int vfs_mkdir(int dirfd, const char *name, mode_t mode) {
  struct path parent;
  char last[NAME_MAX + 1];
  int r = path_parentat(dirfd, name, &parent, last);
  if (r) return r;
  struct dentry *d;
  if (!strcmp(last, ".")) r = -EEXIST;
  else if (lookup_last(&parent, last, &d) == 0) {
    d_put(d);
    r = -EEXIST;
  } else if (!(r = may_create(&parent))) {
    struct inode *dir = parent.dentry->inode;
    struct inode *ni = NULL;
    if (!dir->i_op || !dir->i_op->mkdir) r = -EPERM;
    else {
      mode_t um = current->proc ? current->proc->umask : 022;
      r = dir->i_op->mkdir(dir, last, S_IFDIR | (mode & 07777 & ~um), &ni);
      if (!r) inode_put(ni);
    }
  }
  path_put(&parent);
  return r;
}

static int may_delete(struct path *parent, struct inode *victim) {
  int r = may_create(parent);
  if (r) return r;
  struct inode *dir = parent->dentry->inode;
  struct process *p = current->proc;
  if ((dir->mode & S_ISVTX) && p && p->cred.euid != 0 && p->cred.euid != victim->uid &&
      p->cred.euid != dir->uid)
    return -EPERM;
  return 0;
}

static bool is_mountpoint_busy(struct dentry *d) { return d->mounted != NULL; }

int vfs_unlink(int dirfd, const char *name) {
  struct path parent;
  char last[NAME_MAX + 1];
  int r = path_parentat(dirfd, name, &parent, last);
  if (r) return r;
  struct dentry *d = NULL;
  if (!strcmp(last, ".")) r = -EISDIR;
  else if (!(r = lookup_last(&parent, last, &d))) {
    struct inode *dir = parent.dentry->inode;
    if (S_ISDIR(d->inode->mode)) r = -EISDIR;
    else if (!(r = may_delete(&parent, d->inode))) {
      if (!dir->i_op || !dir->i_op->unlink) r = -EPERM;
      else if (!(r = dir->i_op->unlink(dir, last, d->inode))) d_drop(d);
    }
    d_put(d);
  }
  path_put(&parent);
  return r;
}

int vfs_rmdir(int dirfd, const char *name) {
  struct path parent;
  char last[NAME_MAX + 1];
  int r = path_parentat(dirfd, name, &parent, last);
  if (r) return r;
  struct dentry *d = NULL;
  if (!strcmp(last, ".")) r = -EINVAL;
  else if (!(r = lookup_last(&parent, last, &d))) {
    struct inode *dir = parent.dentry->inode;
    if (!S_ISDIR(d->inode->mode)) r = -ENOTDIR;
    else if (is_mountpoint_busy(d)) r = -EBUSY;
    else if (!(r = may_delete(&parent, d->inode))) {
      if (!dir->i_op || !dir->i_op->rmdir) r = -EPERM;
      else if (!(r = dir->i_op->rmdir(dir, last, d->inode))) d_drop(d);
    }
    d_put(d);
  }
  path_put(&parent);
  return r;
}

static bool is_ancestor(struct dentry *a, struct dentry *d) {
  for (; d; d = d->parent)
    if (d == a) return true;
  return false;
}

int vfs_rename(int olddirfd, const char *old, int newdirfd, const char *newn) {
  struct path op, np;
  char olast[NAME_MAX + 1], nlast[NAME_MAX + 1];
  int r = path_parentat(olddirfd, old, &op, olast);
  if (r) return r;
  r = path_parentat(newdirfd, newn, &np, nlast);
  if (r) {
    path_put(&op);
    return r;
  }
  struct dentry *od = NULL, *nd = NULL;
  if (!strcmp(olast, ".") || !strcmp(nlast, ".")) r = -EBUSY;
  else if (op.mnt != np.mnt) r = -EXDEV;
  else if ((r = lookup_last(&op, olast, &od))) {
  } else if ((r = may_delete(&op, od->inode)) || (r = may_create(&np))) {
  } else {
    lookup_last(&np, nlast, &nd); /* may not exist */
    struct inode *odir = op.dentry->inode, *ndir = np.dentry->inode;
    if (nd && nd == od) r = 0;
    else if (S_ISDIR(od->inode->mode) && is_ancestor(od, np.dentry)) r = -EINVAL;
    else if (nd && S_ISDIR(od->inode->mode) && !S_ISDIR(nd->inode->mode)) r = -ENOTDIR;
    else if (nd && !S_ISDIR(od->inode->mode) && S_ISDIR(nd->inode->mode)) r = -EISDIR;
    else if (nd && is_mountpoint_busy(nd)) r = -EBUSY;
    else if (!odir->i_op || !odir->i_op->rename) r = -EPERM;
    else {
      r = odir->i_op->rename(odir, olast, ndir, nlast, od->inode, nd ? nd->inode : NULL);
      if (!r) {
        if (nd) d_drop(nd);
        /* move the dentry to its new place */
        list_del(&od->sibling);
        struct dentry *oldparent = od->parent;
        od->parent = np.dentry;
        d_get(np.dentry);
        list_add(&od->sibling, &np.dentry->children);
        char *nm = kstrdup(nlast, 0);
        if (nm) {
          kfree(od->name);
          od->name = nm;
        }
        d_put(oldparent);
      }
    }
  }
  if (od) d_put(od);
  if (nd) d_put(nd);
  path_put(&op);
  path_put(&np);
  return r;
}

int vfs_link(int olddirfd, const char *old, int newdirfd, const char *newn, int flags) {
  struct path target;
  int r = path_lookupat(olddirfd, old, (flags & AT_SYMLINK_FOLLOW) ? LOOKUP_FOLLOW : 0, &target);
  if (r) return r;
  struct path parent;
  char last[NAME_MAX + 1];
  r = path_parentat(newdirfd, newn, &parent, last);
  if (r) {
    path_put(&target);
    return r;
  }
  struct dentry *d;
  if (S_ISDIR(target.dentry->inode->mode)) r = -EPERM;
  else if (target.mnt != parent.mnt) r = -EXDEV;
  else if (lookup_last(&parent, last, &d) == 0) {
    d_put(d);
    r = -EEXIST;
  } else if (!(r = may_create(&parent))) {
    struct inode *dir = parent.dentry->inode;
    r = dir->i_op && dir->i_op->link ? dir->i_op->link(dir, last, target.dentry->inode) : -EPERM;
  }
  path_put(&target);
  path_put(&parent);
  return r;
}

int vfs_symlink(const char *tgt, int dirfd, const char *name) {
  struct path parent;
  char last[NAME_MAX + 1];
  int r = path_parentat(dirfd, name, &parent, last);
  if (r) return r;
  struct dentry *d;
  if (!strcmp(last, ".")) r = -EEXIST;
  else if (lookup_last(&parent, last, &d) == 0) {
    d_put(d);
    r = -EEXIST;
  } else if (!(r = may_create(&parent))) {
    struct inode *dir = parent.dentry->inode;
    struct inode *ni = NULL;
    r = dir->i_op && dir->i_op->symlink ? dir->i_op->symlink(dir, last, tgt, &ni) : -EPERM;
    if (!r) inode_put(ni);
  }
  path_put(&parent);
  return r;
}

int vfs_setattr(struct inode *inode, struct iattr *a) {
  if (inode->sb && (inode->sb->flags & SB_RDONLY)) return -EROFS;
  if (inode->i_op && inode->i_op->setattr) return inode->i_op->setattr(inode, a);
  if (a->valid & ATTR_SIZE) return -EPERM;
  if (a->valid & ATTR_MODE) inode->mode = (inode->mode & S_IFMT) | (a->mode & 07777);
  if (a->valid & ATTR_UID) inode->uid = a->uid;
  if (a->valid & ATTR_GID) inode->gid = a->gid;
  if (a->valid & ATTR_ATIME) inode->atime = a->atime;
  if (a->valid & ATTR_MTIME) inode->mtime = a->mtime;
  inode->ctime = current_time();
  return 0;
}

int vfs_truncate(struct inode *inode, loff_t len) {
  if (S_ISDIR(inode->mode)) return -EISDIR;
  if (!S_ISREG(inode->mode)) return -EINVAL;
  if (len < 0) return -EINVAL;
  struct iattr a = {.valid = ATTR_SIZE | ATTR_MTIME, .size = len, .mtime = current_time()};
  return vfs_setattr(inode, &a);
}

/* ---------------- open ---------------- */

struct file *open_path(struct path *p, int flags, int *err) {
  struct inode *inode = p->dentry->inode;
  struct file *f = file_alloc();
  if (!f) {
    *err = -ENOMEM;
    return NULL;
  }
  f->inode = inode;
  inode_get(inode);
  f->path = *p;
  path_get(&f->path);
  f->flags = flags & ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC | O_CLOEXEC);
  int acc = flags & O_ACCMODE;
  f->mode = (acc == O_RDONLY || acc == O_RDWR ? FMODE_READ : 0) | (acc == O_WRONLY || acc == O_RDWR ? FMODE_WRITE : 0);
  if (flags & O_PATH) f->mode = 0;
  f->f_op = inode->f_op;
  if (!(flags & O_PATH)) {
    if (S_ISCHR(inode->mode)) {
      if (p->mnt->sb->flags & SB_NODEV) {
        file_put(f);
        *err = -EACCES;
        return NULL;
      }
      struct cdev *cd = cdev_lookup(inode->rdev);
      if (!cd) {
        file_put(f);
        *err = -ENXIO;
        return NULL;
      }
      f->f_op = cd->fops;
      f->priv = cd->priv;
    } else if (S_ISFIFO(inode->mode)) {
      int r = fifo_open(inode, f);
      if (r) {
        file_put(f);
        *err = r;
        return NULL;
      }
    }
    if (f->f_op && f->f_op->open) {
      int r = f->f_op->open(inode, f);
      if (r) {
        f->f_op = NULL; /* do not call release */
        file_put(f);
        *err = r;
        return NULL;
      }
    }
  }
  return f;
}

struct file *vfs_open(int dirfd, const char *name, int flags, mode_t mode, int *err) {
  struct path p;
  int r;
  bool created = false;
  if (flags & O_CREAT) {
    struct path parent;
    char last[NAME_MAX + 1];
    r = path_parentat(dirfd, name, &parent, last);
    if (r) goto fail;
    struct dentry *d;
    if (!strcmp(last, ".")) {
      path_put(&parent);
      r = -EISDIR;
      goto fail;
    }
    r = lookup_last(&parent, last, &d);
    if (r == -ENOENT) {
      if (!(r = may_create(&parent))) {
        struct inode *dir = parent.dentry->inode;
        struct inode *ni = NULL;
        mode_t um = current->proc ? current->proc->umask : 022;
        r = dir->i_op && dir->i_op->create
                ? dir->i_op->create(dir, last, S_IFREG | (mode & 07777 & ~um), 0, &ni)
                : -EPERM;
        if (!r) {
          inode_put(ni);
          r = lookup_last(&parent, last, &d);
          created = true;
        }
      }
      if (r) {
        path_put(&parent);
        goto fail;
      }
      p.mnt = parent.mnt;
      p.dentry = d;
      path_put(&parent);
    } else if (r == 0) {
      if (flags & O_EXCL) {
        d_put(d);
        path_put(&parent);
        r = -EEXIST;
        goto fail;
      }
      p.mnt = parent.mnt;
      p.dentry = d;
      follow_mounts(&p);
      if (S_ISLNK(p.dentry->inode->mode)) {
        if (flags & O_NOFOLLOW) {
          path_put(&p);
          path_put(&parent);
          r = -ELOOP;
          goto fail;
        }
        /* resolve the symlink from the parent */
        path_put(&p);
        int depth = 0;
        p = parent;
        path_get(&p);
        r = walk(&p, last, LOOKUP_FOLLOW, NULL, &depth);
        if (r) {
          path_put(&p);
          path_put(&parent);
          goto fail;
        }
      }
      path_put(&parent);
    } else {
      path_put(&parent);
      goto fail;
    }
  } else {
    r = path_lookupat(dirfd, name, (flags & O_NOFOLLOW) ? 0 : LOOKUP_FOLLOW, &p);
    if (r) goto fail;
    if ((flags & O_NOFOLLOW) && S_ISLNK(p.dentry->inode->mode) && !(flags & O_PATH)) {
      path_put(&p);
      r = -ELOOP;
      goto fail;
    }
  }

  struct inode *inode = p.dentry->inode;
  int acc = flags & O_ACCMODE;
  if ((flags & O_DIRECTORY) && !S_ISDIR(inode->mode)) r = -ENOTDIR;
  else if (S_ISDIR(inode->mode) && (acc != O_RDONLY || (flags & O_TRUNC))) r = -EISDIR;
  else if (!(flags & O_PATH) && !created) {
    int mask = (acc == O_RDONLY ? 4 : acc == O_WRONLY ? 2 : 6);
    if ((mask & 2) && (p.mnt->sb->flags & SB_RDONLY) && (S_ISREG(inode->mode) || S_ISDIR(inode->mode)))
      r = -EROFS;
    else r = permission(inode, mask);
  }
  if (r) {
    path_put(&p);
    goto fail;
  }
  if ((flags & O_TRUNC) && S_ISREG(inode->mode) && acc != O_RDONLY && !created) {
    r = vfs_truncate(inode, 0);
    if (r) {
      path_put(&p);
      goto fail;
    }
  }
  struct file *f = open_path(&p, flags, err);
  path_put(&p);
  return f;
fail:
  *err = r;
  return NULL;
}

struct file *kernel_open(const char *name, int flags, mode_t mode) {
  int err;
  struct file *f = vfs_open(AT_FDCWD, name, flags, mode, &err);
  return f ? f : ERR_PTR(err);
}

/* ---------------- read / write ---------------- */

ssize_t vfs_read(struct file *f, struct iobuf *b, loff_t *pos) {
  if (!(f->mode & FMODE_READ)) return -EBADF;
  if (!f->f_op || !f->f_op->read) return S_ISDIR(f->inode->mode) ? -EISDIR : -EINVAL;
  if (b->len == 0) return 0;
  return f->f_op->read(f, b, pos);
}

ssize_t vfs_write(struct file *f, struct iobuf *b, loff_t *pos) {
  if (!(f->mode & FMODE_WRITE)) return -EBADF;
  if (!f->f_op || !f->f_op->write) return -EINVAL;
  if (b->len == 0) return 0;
  return f->f_op->write(f, b, pos);
}

ssize_t kernel_pread(struct file *f, void *buf, size_t n, loff_t pos) {
  struct iobuf b = kbuf(buf, n);
  size_t done = 0;
  while (done < n) {
    b.ptr = (u8 *)buf + done;
    b.len = n - done;
    ssize_t r = f->f_op && f->f_op->read ? f->f_op->read(f, &b, &pos) : -EINVAL;
    if (r < 0) return done ? (ssize_t)done : r;
    if (r == 0) break;
    done += r;
  }
  return done;
}

ssize_t kernel_pwrite(struct file *f, const void *buf, size_t n, loff_t pos) {
  struct iobuf b = kbuf((void *)buf, n);
  return f->f_op && f->f_op->write ? f->f_op->write(f, &b, &pos) : -EINVAL;
}

int iob_write(struct iobuf *b, size_t off, const void *src, size_t n) {
  if (off + n > b->len) return -EFAULT;
  if (b->user) return copy_to_user((u64)b->ptr + off, src, n);
  memcpy(b->ptr + off, src, n);
  return 0;
}

int iob_read(struct iobuf *b, size_t off, void *dst, size_t n) {
  if (off + n > b->len) return -EFAULT;
  if (b->user) return copy_from_user(dst, (u64)b->ptr + off, n);
  memcpy(dst, b->ptr + off, n);
  return 0;
}

/* ---------------- mounts ---------------- */

struct mount *root_mount(void) { return root_mnt; }

static struct super_block *sb_alloc(struct fs_type *type) {
  struct super_block *sb = kzalloc(sizeof(*sb), 0);
  if (!sb) return NULL;
  sb->type = type;
  list_init(&sb->inodes);
  static dev_t anon_dev = 1;
  dev_t minor = anon_dev++;
  sb->dev = MKDEV(0, minor);
  return sb;
}

int do_mount(const char *dev, const char *dir, const char *type, u32 flags, const char *data) {
  struct fs_type *fs = find_fs(type);
  if (!fs) return -ENODEV;
  struct path mp = {0};
  if (root_mnt) {
    int r = kern_path(dir, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &mp);
    if (r) return r;
  }
  struct super_block *sb = sb_alloc(fs);
  if (!sb) {
    path_put(&mp);
    return -ENOMEM;
  }
  sb->flags = flags;
  int r = fs->mount(sb, dev, data);
  if (r) {
    kfree(sb);
    path_put(&mp);
    return r;
  }
  struct mount *m = kzalloc(sizeof(*m), 0);
  m->sb = sb;
  m->flags = flags;
  m->devname = kstrdup(dev ? dev : type, 0);
  if (!root_mnt) {
    root_mnt = m;
  } else {
    m->mountpoint = mp.dentry; /* keeps the reference */
    m->parent = mp.mnt;
    mp.dentry->mounted = m;
  }
  list_add_tail(&m->link, &mounts);
  return 0;
}

int do_umount(const char *dir, int flags) {
  struct path p;
  int r = kern_path(dir, LOOKUP_FOLLOW, &p);
  if (r) return r;
  struct mount *m = p.mnt;
  if (p.dentry != m->sb->root || m == root_mnt) {
    path_put(&p);
    return -EINVAL;
  }
  path_put(&p);
  /* busy if anything besides the mount itself references the root */
  if (atomic_read(&m->sb->root->refcount) > 1 && !(flags & 2 /* MNT_DETACH */)) return -EBUSY;
  struct mount *c;
  list_for_each_entry(c, &mounts, link)
    if (c->parent == m) return -EBUSY;
  if (m->sb->s_op && m->sb->s_op->sync) m->sb->s_op->sync(m->sb);
  m->mountpoint->mounted = NULL;
  d_put(m->mountpoint);
  list_del(&m->link);
  if (m->sb->s_op && m->sb->s_op->put_super) m->sb->s_op->put_super(m->sb);
  d_put(m->sb->root);
  kfree(m->devname);
  kfree(m->sb);
  kfree(m);
  return 0;
}

void vfs_sync_all(void) {
  struct mount *m;
  list_for_each_entry(m, &mounts, link)
    if (m->sb->s_op && m->sb->s_op->sync) m->sb->s_op->sync(m->sb);
}

int mounts_show(char *buf, size_t size) {
  size_t n = 0;
  struct mount *m;
  list_for_each_entry(m, &mounts, link) {
    char path[256];
    struct path p = {m, m->sb->root};
    if (d_path(&p, path, sizeof(path)) < 0) strlcpy(path, "?", sizeof(path));
    n += snprintf(buf + n, n < size ? size - n : 0, "%s %s %s %s 0 0\n", m->devname, path, m->sb->type->name,
                  (m->sb->flags & SB_RDONLY) ? "ro" : "rw");
  }
  return (int)MIN(n, size);
}

int vfs_statfs(struct super_block *sb, void *out) {
  struct {
    u64 f_type, f_bsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    s32 f_fsid[2];
    u64 f_namelen, f_frsize, f_flags, f_spare[4];
  } *st = out;
  memset(st, 0, sizeof(*st));
  u64 blocks = 0, bfree = 0, files = 0, ffree = 0;
  u32 bsize = PAGE_SIZE;
  if (sb->s_op && sb->s_op->statfs) sb->s_op->statfs(sb, &blocks, &bfree, &files, &ffree, &bsize);
  st->f_type = 0x4f4c5558; /* "OLUX" */
  st->f_bsize = st->f_frsize = bsize;
  st->f_blocks = blocks;
  st->f_bfree = st->f_bavail = bfree;
  st->f_files = files;
  st->f_ffree = ffree;
  st->f_namelen = NAME_MAX;
  st->f_flags = (sb->flags & SB_RDONLY) ? 1 : 0;
  return 0;
}

void vfs_init(void) {
  int r = do_mount("rootfs", "/", "tmpfs", 0, NULL);
  if (r) panic("cannot mount root tmpfs: %d", r);
}
