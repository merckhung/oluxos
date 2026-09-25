/* Filesystem system calls. All run under the big kernel lock. */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/uaccess.h>

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7
#define F_SETOWN 8
#define F_GETOWN 9
#define F_DUPFD_CLOEXEC 1030
#define F_GETPIPE_SZ 1032
#define FD_CLOEXEC 1
#define FIONCLEX 0x5450
#define FIOCLEX 0x5451
#define FIONBIO 0x5421
#define FIOASYNC 0x5452

static char *getname(u64 uptr, int *err) {
  if (!uptr) {
    *err = -EFAULT;
    return NULL;
  }
  return strndup_user(uptr, PATH_MAX, err);
}

#define GETNAME(var, uptr)                 \
  int __err_##var;                         \
  char *var = getname(uptr, &__err_##var); \
  if (!var) return __err_##var

/* ---------------- open / close ---------------- */

long sys_openat(u64 dfd, u64 upath, u64 flags, u64 mode);
long sys_openat(u64 dfd, u64 upath, u64 flags, u64 mode) {
  GETNAME(path, upath);
  int err;
  struct file *f = vfs_open((int)dfd, path, (int)flags, (mode_t)mode, &err);
  kfree(path);
  if (!f) return err;
  int fd = fd_install(f, 0, flags & O_CLOEXEC);
  if (fd < 0) file_put(f);
  return fd;
}

long sys_close(u64 fd);
long sys_close(u64 fd) { return fd_close((int)fd); }

long sys_dup(u64 fd);
long sys_dup(u64 fd) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  int n = fd_install(f, 0, false);
  if (n < 0) file_put(f);
  return n;
}

long sys_dup3(u64 oldfd, u64 newfd, u64 flags);
long sys_dup3(u64 oldfd, u64 newfd, u64 flags) {
  if (oldfd == newfd) return -EINVAL;
  if (flags & ~O_CLOEXEC) return -EINVAL;
  struct fdtable *t = current->proc->files;
  if ((int)newfd < 0 || (int)newfd >= t->max) return -EBADF;
  struct file *f = fget((int)oldfd);
  if (!f) return -EBADF;
  if (t->fd[newfd]) file_put(t->fd[newfd]);
  t->fd[newfd] = f;
  if (flags & O_CLOEXEC)
    t->cloexec[newfd / 64] |= 1UL << (newfd % 64);
  else
    t->cloexec[newfd / 64] &= ~(1UL << (newfd % 64));
  return newfd;
}

long sys_fcntl(u64 fd, u64 cmd, u64 arg);
long sys_fcntl(u64 fd, u64 cmd, u64 arg) {
  struct fdtable *t = current->proc->files;
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  long r = 0;
  switch (cmd) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
      if (arg >= (u64)t->max) {
        r = -EINVAL;
        break;
      }
      file_get(f);
      r = fd_install(f, (int)arg, cmd == F_DUPFD_CLOEXEC);
      if (r < 0) file_put(f);
      break;
    case F_GETFD:
      r = (t->cloexec[fd / 64] >> (fd % 64)) & 1;
      break;
    case F_SETFD:
      if (arg & FD_CLOEXEC)
        t->cloexec[fd / 64] |= 1UL << (fd % 64);
      else
        t->cloexec[fd / 64] &= ~(1UL << (fd % 64));
      break;
    case F_GETFL:
      r = f->flags | ((f->mode & FMODE_READ) && (f->mode & FMODE_WRITE) ? O_RDWR
                      : (f->mode & FMODE_WRITE)                         ? O_WRONLY
                                                                        : 0);
      r = (r & ~O_ACCMODE) | ((f->mode & FMODE_READ) && (f->mode & FMODE_WRITE) ? O_RDWR
                              : (f->mode & FMODE_WRITE)                         ? O_WRONLY
                                                                                : O_RDONLY);
      break;
    case F_SETFL:
      f->flags = (f->flags & ~(O_APPEND | O_NONBLOCK | O_ASYNC | O_DIRECT | O_NOATIME)) |
                 (arg & (O_APPEND | O_NONBLOCK | O_ASYNC | O_DIRECT | O_NOATIME));
      break;
    case F_GETLK: {
      /* advisory locks are not enforced: report "unlocked" */
      struct {
        s16 type, whence;
        s64 start, len;
        s32 pid;
      } fl;
      if (copy_from_user(&fl, arg, sizeof(fl)))
        r = -EFAULT;
      else {
        fl.type = 2; /* F_UNLCK */
        r = copy_to_user(arg, &fl, sizeof(fl));
      }
      break;
    }
    case F_SETLK:
    case F_SETLKW:
    case F_SETOWN:
      r = 0;
      break;
    case F_GETOWN:
      r = 0;
      break;
    case F_GETPIPE_SZ:
      r = 65536;
      break;
    default:
      r = -EINVAL;
  }
  file_put(f);
  return r;
}

long sys_ioctl(u64 fd, u64 cmd, u64 arg);
long sys_ioctl(u64 fd, u64 cmd, u64 arg) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  long r;
  struct fdtable *t = current->proc->files;
  switch ((unsigned)cmd) {
    case FIOCLEX:
      t->cloexec[fd / 64] |= 1UL << (fd % 64);
      r = 0;
      break;
    case FIONCLEX:
      t->cloexec[fd / 64] &= ~(1UL << (fd % 64));
      r = 0;
      break;
    case FIONBIO: {
      s32 on;
      if (get_user(on, arg)) {
        r = -EFAULT;
        break;
      }
      if (on)
        f->flags |= O_NONBLOCK;
      else
        f->flags &= ~O_NONBLOCK;
      r = 0;
      if (f->f_op && f->f_op->ioctl) f->f_op->ioctl(f, (unsigned)cmd, arg);
      break;
    }
    case FIOASYNC:
      r = 0;
      break;
    default:
      r = f->f_op && f->f_op->ioctl ? f->f_op->ioctl(f, (unsigned)cmd, arg) : -ENOTTY;
  }
  file_put(f);
  return r;
}

long sys_flock(u64 fd, u64 op);
long sys_flock(u64 fd, u64 op) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  file_put(f);
  return 0;
}

/* ---------------- read / write ---------------- */

static bool is_seekable(struct file *f) {
  mode_t m = f->inode ? f->inode->mode : 0;
  if (S_ISCHR(m)) return f->f_op && f->f_op->llseek; /* e.g. /dev/fb0, /dev/mem */
  return !S_ISFIFO(m) && !S_ISSOCK(m);
}

static long rw(u64 fd, struct iobuf *b, bool write, loff_t *explicit_pos) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  if (!access_ok((u64)b->ptr, b->len)) {
    file_put(f);
    return -EFAULT;
  }
  if (b->len > 0x7ffff000) b->len = 0x7ffff000;
  long r;
  if (explicit_pos) {
    if (!is_seekable(f))
      r = -ESPIPE;
    else if (*explicit_pos < 0)
      r = -EINVAL;
    else
      r = write ? vfs_write(f, b, explicit_pos) : vfs_read(f, b, explicit_pos);
  } else {
    loff_t pos = f->pos;
    r = write ? vfs_write(f, b, &pos) : vfs_read(f, b, &pos);
    if (r >= 0) f->pos = pos;
  }
  file_put(f);
  return r;
}

long sys_read(u64 fd, u64 buf, u64 count);
long sys_read(u64 fd, u64 buf, u64 count) {
  struct iobuf b = ubuf(buf, count);
  return rw(fd, &b, false, NULL);
}

long sys_write(u64 fd, u64 buf, u64 count);
long sys_write(u64 fd, u64 buf, u64 count) {
  struct iobuf b = ubuf(buf, count);
  return rw(fd, &b, true, NULL);
}

long sys_pread64(u64 fd, u64 buf, u64 count, u64 pos);
long sys_pread64(u64 fd, u64 buf, u64 count, u64 pos) {
  struct iobuf b = ubuf(buf, count);
  loff_t p = (loff_t)pos;
  return rw(fd, &b, false, &p);
}

long sys_pwrite64(u64 fd, u64 buf, u64 count, u64 pos);
long sys_pwrite64(u64 fd, u64 buf, u64 count, u64 pos) {
  struct iobuf b = ubuf(buf, count);
  loff_t p = (loff_t)pos;
  return rw(fd, &b, true, &p);
}

struct iovec {
  u64 base, len;
};

static long rwv(u64 fd, u64 uiov, u64 cnt, bool write, loff_t *pos) {
  if (cnt > 1024) return -EINVAL;
  long total = 0;
  for (u64 i = 0; i < cnt; i++) {
    struct iovec v;
    if (copy_from_user(&v, uiov + i * sizeof(v), sizeof(v))) return total ? total : -EFAULT;
    if (!v.len) continue;
    struct iobuf b = ubuf(v.base, v.len);
    long r = rw(fd, &b, write, pos);
    if (r < 0) return total ? total : r;
    total += r;
    if (pos) *pos += 0; /* rw advanced *pos */
    if ((u64)r < v.len) break;
  }
  return total;
}

long sys_readv(u64 fd, u64 iov, u64 cnt);
long sys_readv(u64 fd, u64 iov, u64 cnt) { return rwv(fd, iov, cnt, false, NULL); }
long sys_writev(u64 fd, u64 iov, u64 cnt);
long sys_writev(u64 fd, u64 iov, u64 cnt) { return rwv(fd, iov, cnt, true, NULL); }
long sys_preadv(u64 fd, u64 iov, u64 cnt, u64 pos);
long sys_preadv(u64 fd, u64 iov, u64 cnt, u64 pos) {
  loff_t p = (loff_t)pos;
  return rwv(fd, iov, cnt, false, &p);
}
long sys_pwritev(u64 fd, u64 iov, u64 cnt, u64 pos);
long sys_pwritev(u64 fd, u64 iov, u64 cnt, u64 pos) {
  loff_t p = (loff_t)pos;
  return rwv(fd, iov, cnt, true, &p);
}

long sys_lseek(u64 fd, u64 off, u64 whence);
long sys_lseek(u64 fd, u64 off, u64 whence) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  long r;
  if (!is_seekable(f))
    r = -ESPIPE;
  else if (f->f_op && f->f_op->llseek)
    r = f->f_op->llseek(f, (loff_t)off, (int)whence);
  else {
    loff_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos : whence == SEEK_END ? f->inode->size : -1;
    if (base < 0)
      r = -EINVAL;
    else if (base + (loff_t)off < 0)
      r = -EINVAL;
    else
      r = f->pos = base + (loff_t)off;
  }
  file_put(f);
  return r;
}

long sys_sendfile(u64 outfd, u64 infd, u64 uoff, u64 count);
long sys_sendfile(u64 outfd, u64 infd, u64 uoff, u64 count) {
  struct file *in = fget((int)infd), *out = fget((int)outfd);
  long r = 0;
  if (!in || !out) {
    r = -EBADF;
    goto done;
  }
  loff_t pos = in->pos;
  if (uoff && get_user(pos, uoff)) {
    r = -EFAULT;
    goto done;
  }
  u8 *buf = kmalloc(PAGE_SIZE, 0);
  if (!buf) {
    r = -ENOMEM;
    goto done;
  }
  long total = 0;
  while ((u64)total < count) {
    size_t n = MIN(count - total, (u64)PAGE_SIZE);
    struct iobuf b = kbuf(buf, n);
    ssize_t got = vfs_read(in, &b, &pos);
    if (got <= 0) {
      if (got < 0 && !total) total = got;
      break;
    }
    struct iobuf w = kbuf(buf, got);
    loff_t opos = out->pos;
    ssize_t put = vfs_write(out, &w, &opos);
    if (put > 0) out->pos = opos;
    if (put <= 0) {
      if (!total) total = put;
      break;
    }
    total += put;
    if (put < got) break;
  }
  kfree(buf);
  if (total > 0 || !uoff) {
    if (uoff)
      put_user(pos, uoff);
    else
      in->pos = pos;
  }
  r = total;
done:
  if (in) file_put(in);
  if (out) file_put(out);
  return r;
}

/* ---------------- directories ---------------- */

struct getdents_ctx {
  struct dir_context ctx;
  u64 ubuf;
  size_t size, used;
  int err;
};

static bool filldir64(struct dir_context *c, const char *name, size_t len, ino_t ino, unsigned type) {
  struct getdents_ctx *g = container_of(c, struct getdents_ctx, ctx);
  size_t reclen = ALIGN_UP(19 + len + 1, 8);
  if (g->used + reclen > g->size) {
    if (!g->used) g->err = -EINVAL;
    return false;
  }
  u8 rec[19 + NAME_MAX + 9];
  memset(rec, 0, reclen);
  *(u64 *)rec = ino;
  s64 off = c->pos + 1;
  memcpy(rec + 8, &off, 8);
  u16 rl = (u16)reclen;
  memcpy(rec + 16, &rl, 2);
  rec[18] = (u8)type;
  memcpy(rec + 19, name, len);
  if (copy_to_user(g->ubuf + g->used, rec, reclen)) {
    g->err = -EFAULT;
    return false;
  }
  g->used += reclen;
  return true;
}

long sys_getdents64(u64 fd, u64 dirp, u64 count);
long sys_getdents64(u64 fd, u64 dirp, u64 count) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  long r;
  if (!S_ISDIR(f->inode->mode))
    r = -ENOTDIR;
  else if (!f->f_op || !f->f_op->iterate)
    r = -ENOTDIR;
  else {
    struct getdents_ctx g = {.ctx = {.actor = filldir64, .pos = f->pos}, .ubuf = dirp, .size = count};
    r = f->f_op->iterate(f, &g.ctx);
    f->pos = g.ctx.pos;
    if (!r) r = g.err && !g.used ? g.err : (long)g.used;
  }
  file_put(f);
  return r;
}

long sys_getcwd(u64 buf, u64 size);
long sys_getcwd(u64 buf, u64 size) {
  char *k = kmalloc(PATH_MAX, 0);
  if (!k) return -ENOMEM;
  int n = d_path(&current->proc->cwd, k, PATH_MAX);
  long r;
  if (n < 0)
    r = n;
  else if ((u64)n + 1 > size)
    r = -ERANGE;
  else
    r = copy_to_user(buf, k, n + 1) ? -EFAULT : n + 1;
  kfree(k);
  return r;
}

long sys_chdir(u64 upath);
long sys_chdir(u64 upath) {
  GETNAME(path, upath);
  struct path p;
  int r = kern_path(path, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &p);
  kfree(path);
  if (r) return r;
  r = permission(p.dentry->inode, 1);
  if (r) {
    path_put(&p);
    return r;
  }
  path_put(&current->proc->cwd);
  current->proc->cwd = p;
  return 0;
}

long sys_fchdir(u64 fd);
long sys_fchdir(u64 fd) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  long r = 0;
  if (!S_ISDIR(f->inode->mode))
    r = -ENOTDIR;
  else {
    path_put(&current->proc->cwd);
    current->proc->cwd = f->path;
    path_get(&current->proc->cwd);
  }
  file_put(f);
  return r;
}

long sys_chroot(u64 upath);
long sys_chroot(u64 upath) {
  if (!capable_root()) return -EPERM;
  GETNAME(path, upath);
  struct path p;
  int r = kern_path(path, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &p);
  kfree(path);
  if (r) return r;
  path_put(&current->proc->root);
  current->proc->root = p;
  return 0;
}

long sys_mkdirat(u64 dfd, u64 upath, u64 mode);
long sys_mkdirat(u64 dfd, u64 upath, u64 mode) {
  GETNAME(path, upath);
  int r = vfs_mkdir((int)dfd, path, (mode_t)mode);
  kfree(path);
  return r;
}

long sys_mknodat(u64 dfd, u64 upath, u64 mode, u64 dev);
long sys_mknodat(u64 dfd, u64 upath, u64 mode, u64 dev) {
  mode_t m = (mode_t)mode;
  if ((S_ISCHR(m) || S_ISBLK(m)) && !capable_root()) return -EPERM;
  if (!(m & S_IFMT)) m |= S_IFREG;
  if (S_ISDIR(m) || S_ISLNK(m)) return -EINVAL;
  GETNAME(path, upath);
  int r = vfs_mknod((int)dfd, path, m, (dev_t)dev);
  kfree(path);
  return r;
}

long sys_unlinkat(u64 dfd, u64 upath, u64 flags);
long sys_unlinkat(u64 dfd, u64 upath, u64 flags) {
  GETNAME(path, upath);
  int r = (flags & AT_REMOVEDIR) ? vfs_rmdir((int)dfd, path) : vfs_unlink((int)dfd, path);
  kfree(path);
  return r;
}

long sys_symlinkat(u64 utarget, u64 dfd, u64 upath);
long sys_symlinkat(u64 utarget, u64 dfd, u64 upath) {
  GETNAME(target, utarget);
  int err;
  char *path = getname(upath, &err);
  if (!path) {
    kfree(target);
    return err;
  }
  int r = vfs_symlink(target, (int)dfd, path);
  kfree(target);
  kfree(path);
  return r;
}

long sys_linkat(u64 odfd, u64 uold, u64 ndfd, u64 unew, u64 flags);
long sys_linkat(u64 odfd, u64 uold, u64 ndfd, u64 unew, u64 flags) {
  GETNAME(o, uold);
  int err;
  char *n = getname(unew, &err);
  if (!n) {
    kfree(o);
    return err;
  }
  int r = vfs_link((int)odfd, o, (int)ndfd, n, (int)flags);
  kfree(o);
  kfree(n);
  return r;
}

long sys_renameat2(u64 odfd, u64 uold, u64 ndfd, u64 unew, u64 flags);
long sys_renameat2(u64 odfd, u64 uold, u64 ndfd, u64 unew, u64 flags) {
  if (flags & ~1UL) return -EINVAL; /* RENAME_NOREPLACE only */
  GETNAME(o, uold);
  int err;
  char *n = getname(unew, &err);
  if (!n) {
    kfree(o);
    return err;
  }
  int r = 0;
  if (flags & 1) {
    struct path p;
    if (!path_lookupat((int)ndfd, n, 0, &p)) {
      path_put(&p);
      r = -EEXIST;
    }
  }
  if (!r) r = vfs_rename((int)odfd, o, (int)ndfd, n);
  kfree(o);
  kfree(n);
  return r;
}

long sys_renameat(u64 odfd, u64 uold, u64 ndfd, u64 unew);
long sys_renameat(u64 odfd, u64 uold, u64 ndfd, u64 unew) { return sys_renameat2(odfd, uold, ndfd, unew, 0); }

long sys_readlinkat(u64 dfd, u64 upath, u64 buf, u64 size);
long sys_readlinkat(u64 dfd, u64 upath, u64 buf, u64 size) {
  if ((s64)size <= 0) return -EINVAL;
  GETNAME(path, upath);
  struct path p;
  int r = path_lookupat((int)dfd, path, 0, &p);
  kfree(path);
  if (r) return r;
  struct inode *i = p.dentry->inode;
  long ret;
  if (!S_ISLNK(i->mode) || !i->i_op || !i->i_op->readlink)
    ret = -EINVAL;
  else {
    char *k = kmalloc(PATH_MAX, 0);
    ssize_t n = k ? i->i_op->readlink(i, k, MIN(size, (u64)PATH_MAX)) : -ENOMEM;
    ret = n < 0 ? n : (copy_to_user(buf, k, n) ? -EFAULT : n);
    kfree(k);
  }
  path_put(&p);
  return ret;
}

/* ---------------- stat ---------------- */

struct kstat {
  u64 st_dev, st_ino;
  u32 st_mode, st_nlink, st_uid, st_gid;
  u64 st_rdev, pad1;
  s64 st_size;
  s32 st_blksize, pad2;
  s64 st_blocks;
  s64 st_atime;
  u64 st_atime_nsec;
  s64 st_mtime;
  u64 st_mtime_nsec;
  s64 st_ctime;
  u64 st_ctime_nsec;
  u32 unused4, unused5;
};
STATIC_ASSERT(sizeof(struct kstat) == 128, "struct stat layout");

static void fill_stat(struct inode *i, struct kstat *st) {
  memset(st, 0, sizeof(*st));
  st->st_dev = i->sb ? i->sb->dev : 0;
  st->st_ino = i->ino;
  st->st_mode = i->mode;
  st->st_nlink = i->nlink;
  st->st_uid = i->uid;
  st->st_gid = i->gid;
  st->st_rdev = i->rdev;
  st->st_size = i->size;
  st->st_blksize = PAGE_SIZE;
  st->st_blocks = i->blocks ? (s64)i->blocks : DIV_ROUND_UP(i->size, 512);
  st->st_atime = i->atime.tv_sec;
  st->st_atime_nsec = i->atime.tv_nsec;
  st->st_mtime = i->mtime.tv_sec;
  st->st_mtime_nsec = i->mtime.tv_nsec;
  st->st_ctime = i->ctime.tv_sec;
  st->st_ctime_nsec = i->ctime.tv_nsec;
}

static int stat_at(int dfd, u64 upath, int flags, struct inode **out, struct path *pout) {
  int err;
  char *path = getname(upath, &err);
  if (!path) return err;
  if (!path[0] && (flags & AT_EMPTY_PATH)) {
    kfree(path);
    struct file *f = fget(dfd);
    if (!f) return -EBADF;
    *out = f->inode;
    inode_get(*out);
    pout->dentry = NULL;
    file_put(f);
    return 0;
  }
  int r = path_lookupat(dfd, path, (flags & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW, pout);
  kfree(path);
  if (r) return r;
  *out = pout->dentry->inode;
  inode_get(*out);
  return 0;
}

long sys_newfstatat(u64 dfd, u64 upath, u64 statbuf, u64 flags);
long sys_newfstatat(u64 dfd, u64 upath, u64 statbuf, u64 flags) {
  struct inode *i;
  struct path p = {0};
  int r = stat_at((int)dfd, upath, (int)flags, &i, &p);
  if (r) return r;
  struct kstat st;
  fill_stat(i, &st);
  inode_put(i);
  path_put(&p);
  return copy_to_user(statbuf, &st, sizeof(st));
}

long sys_fstat(u64 fd, u64 statbuf);
long sys_fstat(u64 fd, u64 statbuf) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  struct kstat st;
  fill_stat(f->inode, &st);
  file_put(f);
  return copy_to_user(statbuf, &st, sizeof(st));
}

long sys_statx(u64 dfd, u64 upath, u64 flags, u64 mask, u64 ubuf);
long sys_statx(u64 dfd, u64 upath, u64 flags, u64 mask, u64 ubuf) {
  struct inode *i;
  struct path p = {0};
  int r = stat_at((int)dfd, upath, (int)flags, &i, &p);
  if (r) return r;
  struct {
    u32 mask, blksize;
    u64 attributes;
    u32 nlink, uid, gid;
    u16 mode, pad1;
    u64 ino, size, blocks, attributes_mask;
    struct {
      s64 sec;
      u32 nsec;
      s32 pad;
    } atime, btime, ctime, mtime;
    u32 rdev_major, rdev_minor, dev_major, dev_minor;
    u64 spare[14];
  } sx;
  memset(&sx, 0, sizeof(sx));
  sx.mask = 0x7ff;
  sx.blksize = PAGE_SIZE;
  sx.nlink = i->nlink;
  sx.uid = i->uid;
  sx.gid = i->gid;
  sx.mode = (u16)i->mode;
  sx.ino = i->ino;
  sx.size = i->size;
  sx.blocks = i->blocks ? i->blocks : DIV_ROUND_UP(i->size, 512);
  sx.atime.sec = i->atime.tv_sec;
  sx.atime.nsec = i->atime.tv_nsec;
  sx.mtime.sec = i->mtime.tv_sec;
  sx.mtime.nsec = i->mtime.tv_nsec;
  sx.ctime.sec = i->ctime.tv_sec;
  sx.ctime.nsec = i->ctime.tv_nsec;
  sx.btime = sx.ctime;
  sx.rdev_major = MAJOR(i->rdev);
  sx.rdev_minor = MINOR(i->rdev);
  dev_t d = i->sb ? i->sb->dev : 0;
  sx.dev_major = MAJOR(d);
  sx.dev_minor = MINOR(d);
  inode_put(i);
  path_put(&p);
  return copy_to_user(ubuf, &sx, sizeof(sx));
}

long sys_faccessat(u64 dfd, u64 upath, u64 mode);
long sys_faccessat(u64 dfd, u64 upath, u64 mode) {
  GETNAME(path, upath);
  struct path p;
  int r = path_lookupat((int)dfd, path, LOOKUP_FOLLOW, &p);
  kfree(path);
  if (r) return r;
  if (mode & 7) r = permission(p.dentry->inode, (int)(mode & 7));
  if (!r && (mode & 2) && (p.mnt->sb->flags & SB_RDONLY)) r = -EROFS;
  path_put(&p);
  return r;
}

long sys_faccessat2(u64 dfd, u64 upath, u64 mode, u64 flags);
long sys_faccessat2(u64 dfd, u64 upath, u64 mode, u64 flags) { return sys_faccessat(dfd, upath, mode); }

/* ---------------- attributes ---------------- */

static long setattr_path(int dfd, u64 upath, int follow, struct iattr *a) {
  GETNAME(path, upath);
  struct path p;
  int r = path_lookupat(dfd, path, follow ? LOOKUP_FOLLOW : 0, &p);
  kfree(path);
  if (r) return r;
  struct inode *i = p.dentry->inode;
  struct cred *c = &current->proc->cred;
  if (c->euid != 0 && c->euid != i->uid)
    r = -EPERM;
  else
    r = vfs_setattr(i, a);
  path_put(&p);
  return r;
}

long sys_fchmodat(u64 dfd, u64 upath, u64 mode);
long sys_fchmodat(u64 dfd, u64 upath, u64 mode) {
  struct iattr a = {.valid = ATTR_MODE, .mode = (mode_t)mode};
  return setattr_path((int)dfd, upath, 1, &a);
}

long sys_fchmod(u64 fd, u64 mode);
long sys_fchmod(u64 fd, u64 mode) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  struct iattr a = {.valid = ATTR_MODE, .mode = (mode_t)mode};
  struct cred *c = &current->proc->cred;
  long r = (c->euid != 0 && c->euid != f->inode->uid) ? -EPERM : vfs_setattr(f->inode, &a);
  file_put(f);
  return r;
}

static void chown_attr(struct iattr *a, u64 uid, u64 gid) {
  a->valid = 0;
  if ((u32)uid != 0xffffffff) {
    a->valid |= ATTR_UID;
    a->uid = (uid_t)uid;
  }
  if ((u32)gid != 0xffffffff) {
    a->valid |= ATTR_GID;
    a->gid = (gid_t)gid;
  }
}

long sys_fchownat(u64 dfd, u64 upath, u64 uid, u64 gid, u64 flags);
long sys_fchownat(u64 dfd, u64 upath, u64 uid, u64 gid, u64 flags) {
  if (!capable_root()) return -EPERM;
  struct iattr a;
  chown_attr(&a, uid, gid);
  return setattr_path((int)dfd, upath, !(flags & AT_SYMLINK_NOFOLLOW), &a);
}

long sys_fchown(u64 fd, u64 uid, u64 gid);
long sys_fchown(u64 fd, u64 uid, u64 gid) {
  if (!capable_root()) return -EPERM;
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  struct iattr a;
  chown_attr(&a, uid, gid);
  long r = vfs_setattr(f->inode, &a);
  file_put(f);
  return r;
}

#define UTIME_NOW ((1L << 30) - 1)
#define UTIME_OMIT ((1L << 30) - 2)

long sys_utimensat(u64 dfd, u64 upath, u64 utimes, u64 flags);
long sys_utimensat(u64 dfd, u64 upath, u64 utimes, u64 flags) {
  struct timespec64 ts[2];
  struct timespec64 now = current_time();
  if (utimes) {
    if (copy_from_user(ts, utimes, sizeof(ts))) return -EFAULT;
  } else {
    ts[0].tv_nsec = ts[1].tv_nsec = UTIME_NOW;
  }
  struct iattr a = {0};
  for (int k = 0; k < 2; k++) {
    if (ts[k].tv_nsec == UTIME_OMIT) continue;
    struct timespec64 v = ts[k].tv_nsec == UTIME_NOW ? now : ts[k];
    if (k == 0) {
      a.valid |= ATTR_ATIME;
      a.atime = v;
    } else {
      a.valid |= ATTR_MTIME;
      a.mtime = v;
    }
  }
  if (!upath) {
    struct file *f = fget((int)dfd);
    if (!f) return -EBADF;
    long r = vfs_setattr(f->inode, &a);
    file_put(f);
    return r;
  }
  GETNAME(path, upath);
  struct path p;
  int r = path_lookupat((int)dfd, path, (flags & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW, &p);
  kfree(path);
  if (r) return r;
  r = vfs_setattr(p.dentry->inode, &a);
  path_put(&p);
  return r;
}

long sys_truncate(u64 upath, u64 len);
long sys_truncate(u64 upath, u64 len) {
  GETNAME(path, upath);
  struct path p;
  int r = kern_path(path, LOOKUP_FOLLOW, &p);
  kfree(path);
  if (r) return r;
  r = permission(p.dentry->inode, 2);
  if (!r) r = vfs_truncate(p.dentry->inode, (loff_t)len);
  path_put(&p);
  return r;
}

long sys_ftruncate(u64 fd, u64 len);
long sys_ftruncate(u64 fd, u64 len) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  long r = (f->mode & FMODE_WRITE) ? vfs_truncate(f->inode, (loff_t)len) : -EINVAL;
  file_put(f);
  return r;
}

long sys_fallocate(u64 fd, u64 mode, u64 off, u64 len);
long sys_fallocate(u64 fd, u64 mode, u64 off, u64 len) {
  if (mode) return -EOPNOTSUPP;
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  long r = 0;
  if ((loff_t)(off + len) > f->inode->size) r = vfs_truncate(f->inode, (loff_t)(off + len));
  file_put(f);
  return r;
}

/* ---------------- misc ---------------- */

long sys_pipe2(u64 ufds, u64 flags);
long sys_pipe2(u64 ufds, u64 flags) {
  if (flags & ~(O_CLOEXEC | O_NONBLOCK | O_DIRECT)) return -EINVAL;
  struct file *w;
  struct file *r = pipe_create_pair(&w, (int)flags);
  if (!r) return -ENOMEM;
  int fr = fd_install(r, 0, flags & O_CLOEXEC);
  if (fr < 0) {
    file_put(r);
    file_put(w);
    return fr;
  }
  int fw = fd_install(w, 0, flags & O_CLOEXEC);
  if (fw < 0) {
    fd_close(fr);
    file_put(w);
    return fw;
  }
  s32 fds[2] = {fr, fw};
  if (copy_to_user(ufds, fds, sizeof(fds))) {
    fd_close(fr);
    fd_close(fw);
    return -EFAULT;
  }
  return 0;
}

long sys_sync(void);
long sys_sync(void) {
  vfs_sync_all();
  return 0;
}

long sys_fsync(u64 fd);
long sys_fsync(u64 fd) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  long r = 0;
  if (f->f_op && f->f_op->fsync)
    r = f->f_op->fsync(f);
  else if (f->inode->sb && f->inode->sb->s_op && f->inode->sb->s_op->sync)
    r = f->inode->sb->s_op->sync(f->inode->sb);
  file_put(f);
  return r;
}

long sys_fdatasync(u64 fd);
long sys_fdatasync(u64 fd) { return sys_fsync(fd); }

long sys_fadvise64(u64 fd, u64 off, u64 len, u64 advice);
long sys_fadvise64(u64 fd, u64 off, u64 len, u64 advice) { return 0; }

int userfs_try_attach(const char *dir, const char *data);

#define MS_RDONLY 1
#define MS_NOSUID 2
#define MS_NODEV 4
#define MS_NOEXEC 8
#define MS_REMOUNT 32

long sys_mount(u64 udev, u64 udir, u64 utype, u64 flags, u64 udata);
long sys_mount(u64 udev, u64 udir, u64 utype, u64 flags, u64 udata) {
  if (!capable_root()) return -EPERM;
  int err;
  char *dir = getname(udir, &err);
  if (!dir) return err;
  char *dev = udev ? getname(udev, &err) : NULL;
  char *type = utype ? getname(utype, &err) : NULL;
  char *data = udata ? getname(udata, &err) : NULL;
  u32 sbf = ((flags & MS_RDONLY) ? SB_RDONLY : 0) | ((flags & MS_NOEXEC) ? SB_NOEXEC : 0) |
            ((flags & MS_NOSUID) ? SB_NOSUID : 0) | ((flags & MS_NODEV) ? SB_NODEV : 0);
  long r;
  if (flags & MS_REMOUNT) {
    struct path p;
    r = kern_path(dir, LOOKUP_FOLLOW, &p);
    if (!r) {
      if (p.dentry != p.mnt->sb->root)
        r = -EINVAL;
      else {
        if (p.mnt->sb->s_op && p.mnt->sb->s_op->sync) p.mnt->sb->s_op->sync(p.mnt->sb);
        p.mnt->sb->flags = (p.mnt->sb->flags & ~(SB_RDONLY | SB_NOEXEC | SB_NOSUID | SB_NODEV)) | sbf;
      }
      path_put(&p);
    }
  } else if (!type) {
    r = -EINVAL;
  } else if (!strcmp(type, "userfs") && data && strstr(data, "attach") && (r = userfs_try_attach(dir, data)) != 1) {
    /* re-attached (or failed to re-attach) an existing userfs mount */
  } else {
    r = do_mount(dev, dir, type, sbf, data);
  }
  kfree(dir);
  kfree(dev);
  kfree(type);
  kfree(data);
  return r;
}

long sys_umount2(u64 udir, u64 flags);
long sys_umount2(u64 udir, u64 flags) {
  if (!capable_root()) return -EPERM;
  GETNAME(dir, udir);
  int r = do_umount(dir, (int)flags);
  kfree(dir);
  return r;
}

long sys_statfs(u64 upath, u64 buf);
long sys_statfs(u64 upath, u64 buf) {
  GETNAME(path, upath);
  struct path p;
  int r = kern_path(path, LOOKUP_FOLLOW, &p);
  kfree(path);
  if (r) return r;
  u8 st[120];
  vfs_statfs(p.mnt->sb, st);
  path_put(&p);
  return copy_to_user(buf, st, sizeof(st));
}

long sys_fstatfs(u64 fd, u64 buf);
long sys_fstatfs(u64 fd, u64 buf) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  u8 st[120];
  if (f->path.mnt)
    vfs_statfs(f->path.mnt->sb, st);
  else
    memset(st, 0, sizeof(st));
  file_put(f);
  return copy_to_user(buf, st, sizeof(st));
}

/* ---------------- eventfd ---------------- */

struct eventfd {
  u64 count;
  bool semaphore;
  struct wait_queue wq;
};

static ssize_t eventfd_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct eventfd *e = f->priv;
  if (b->len < 8) return -EINVAL;
  if (!e->count) {
    if (f->flags & O_NONBLOCK) return -EAGAIN;
    int r = wait_event_interruptible(e->wq, e->count != 0);
    if (r) return r;
  }
  u64 v = e->semaphore ? 1 : e->count;
  e->count -= v;
  wake_up(&e->wq);
  return iob_write(b, 0, &v, 8) ? -EFAULT : 8;
}

static ssize_t eventfd_write(struct file *f, struct iobuf *b, loff_t *pos) {
  struct eventfd *e = f->priv;
  u64 v;
  if (b->len < 8) return -EINVAL;
  if (iob_read(b, 0, &v, 8)) return -EFAULT;
  if (v == ~0ULL) return -EINVAL;
  if (~0ULL - 1 - e->count < v) {
    if (f->flags & O_NONBLOCK) return -EAGAIN;
    int r = wait_event_interruptible(e->wq, ~0ULL - 1 - e->count >= v);
    if (r) return r;
  }
  e->count += v;
  wake_up(&e->wq);
  return 8;
}

static unsigned eventfd_poll(struct file *f, struct poll_table *pt) {
  struct eventfd *e = f->priv;
  poll_wait(f, &e->wq, pt);
  unsigned m = 0;
  if (e->count) m |= POLLIN | POLLRDNORM;
  if (e->count < ~0ULL - 1) m |= POLLOUT | POLLWRNORM;
  return m;
}

static int eventfd_release(struct inode *i, struct file *f) {
  kfree(f->priv);
  return 0;
}

static const struct file_operations eventfd_fops = {
    .read = eventfd_read, .write = eventfd_write, .poll = eventfd_poll, .release = eventfd_release};

static struct super_block anon_sb;

struct inode *anon_inode(mode_t mode);
struct inode *anon_inode(mode_t mode) {
  if (!anon_sb.type) {
    static struct fs_type anonfs = {.name = "anon_inodefs"};
    anon_sb.type = &anonfs;
    list_init(&anon_sb.inodes);
  }
  return new_inode(&anon_sb, mode);
}

long sys_eventfd2(u64 initval, u64 flags);
long sys_eventfd2(u64 initval, u64 flags) {
  struct eventfd *e = kzalloc(sizeof(*e), 0);
  if (!e) return -ENOMEM;
  e->count = (u32)initval;
  e->semaphore = flags & 1;
  wq_init(&e->wq);
  struct file *f = file_alloc();
  if (!f) {
    kfree(e);
    return -ENOMEM;
  }
  f->inode = anon_inode(0600);
  f->f_op = &eventfd_fops;
  f->priv = e;
  f->mode = FMODE_READ | FMODE_WRITE;
  f->flags = O_RDWR | (flags & O_NONBLOCK);
  int fd = fd_install(f, 0, flags & O_CLOEXEC);
  if (fd < 0) file_put(f);
  return fd;
}

/* memfd: an unlinked file on a private tmpfs instance */
long sys_memfd_create(u64 uname, u64 flags);
long sys_memfd_create(u64 uname, u64 flags) {
  static int seq;
  char path[64];
  snprintf(path, sizeof(path), "/dev/shm/.memfd.%d.%d", current->proc->pid, seq++);
  int err;
  struct file *f = vfs_open(AT_FDCWD, path, O_RDWR | O_CREAT | O_EXCL, 0600, &err);
  if (!f) return err;
  vfs_unlink(AT_FDCWD, path);
  int fd = fd_install(f, 0, flags & 1 /* MFD_CLOEXEC */);
  if (fd < 0) file_put(f);
  return fd;
}
