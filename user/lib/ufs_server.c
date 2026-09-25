#include <errno.h>
#include <fcntl.h>
#include <olux_ipc.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <ufs_server.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;
static void on_term(int sig) { stop_requested = 1; }

void ufs_log(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + 3, sizeof(buf) - 3, fmt, ap);
  va_end(ap);
  memcpy(buf, "<6>", 3);
  if (n > (int)sizeof(buf) - 4) n = sizeof(buf) - 4;
  int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
  if (fd >= 0) {
    if (write(fd, buf, n + 3) < 0) {
    }
    close(fd);
  }
}

/* Is `dir` currently the root of a userfs mount? */
static bool is_userfs_mount(const char *dir) {
  FILE *f = fopen("/proc/mounts", "r");
  if (!f) return false;
  char line[512], dev[128], mnt[256], type[32];
  bool found = false;
  while (fgets(line, sizeof(line), f))
    if (sscanf(line, "%127s %255s %31s", dev, mnt, type) == 3 && !strcmp(mnt, dir) && !strcmp(type, "userfs"))
      found = true;
  fclose(f);
  return found;
}

struct fill_ctx {
  uint8_t *buf;
  size_t cap, used;
};

static bool filler(void *p, uint64_t ino, uint64_t next, uint8_t type, const char *name, size_t namelen) {
  struct fill_ctx *f = p;
  if (namelen > UFS_NAME_MAX) namelen = UFS_NAME_MAX;
  size_t rec = (sizeof(struct ufs_dirent) + namelen + 7) & ~7UL;
  if (f->used + rec > f->cap) return false;
  struct ufs_dirent *d = (struct ufs_dirent *)(f->buf + f->used);
  memset(d, 0, rec);
  d->ino = ino;
  d->next = next;
  d->reclen = (uint16_t)rec;
  d->type = type;
  d->namelen = (uint8_t)namelen;
  memcpy(d->name, name, namelen);
  f->used += rec;
  return true;
}

static void finish(const struct ufs_ops *ops, void *ctx) {
  if (ops->shutdown)
    ops->shutdown(ctx);
  else if (ops->sync)
    ops->sync(ctx);
}

static void handle(const struct ufs_ops *ops, void *ctx, struct ufs_req *q, size_t qlen, struct ufs_reply *r) {
  uint8_t *data = (uint8_t *)(r + 1);
  const uint8_t *qdata = (const uint8_t *)(q + 1);
  q->name[UFS_NAME_MAX] = 0;
  q->name2[sizeof(q->name2) - 1] = 0;
  int err = 0;
  ssize_t n;
  switch (q->op) {
    case UFS_HELLO:
      err = ops->root(ctx, &r->attr);
      break;
    case UFS_LOOKUP:
      err = ops->lookup(ctx, q->ino, q->name, &r->attr);
      break;
    case UFS_GETATTR:
      err = ops->getattr(ctx, q->ino, &r->attr);
      break;
    case UFS_READ:
      if (q->len > UFS_MAX_IO) q->len = UFS_MAX_IO;
      n = ops->read(ctx, q->ino, q->off, data, q->len, &r->attr);
      if (n < 0)
        err = (int)n;
      else
        r->len = (uint32_t)n;
      break;
    case UFS_WRITE:
      if (!ops->write) {
        err = -EROFS;
        break;
      }
      if (q->len > UFS_MAX_IO || q->len > qlen - sizeof(*q)) {
        err = -EINVAL;
        break;
      }
      n = ops->write(ctx, q->ino, q->off, qdata, q->len, &r->attr);
      if (n < 0)
        err = (int)n;
      else
        r->count = (uint64_t)n;
      break;
    case UFS_READDIR: {
      struct fill_ctx f = {data, q->len > UFS_MAX_IO ? UFS_MAX_IO : q->len, 0};
      err = ops->readdir(ctx, q->ino, q->off, filler, &f);
      r->len = (uint32_t)f.used;
      break;
    }
    case UFS_CREATE:
      err = ops->create ? ops->create(ctx, q->ino, q->name, q->mode, &r->attr) : -EROFS;
      break;
    case UFS_MKDIR:
      err = ops->mkdir ? ops->mkdir(ctx, q->ino, q->name, q->mode, &r->attr) : -EROFS;
      break;
    case UFS_UNLINK:
      err = ops->unlink ? ops->unlink(ctx, q->ino, q->name) : -EROFS;
      break;
    case UFS_RMDIR:
      err = ops->rmdir ? ops->rmdir(ctx, q->ino, q->name) : -EROFS;
      break;
    case UFS_RENAME:
      err = ops->rename ? ops->rename(ctx, q->ino, q->name, q->ino2, q->name2) : -EROFS;
      break;
    case UFS_SETATTR:
      err = ops->setattr ? ops->setattr(ctx, q->ino, q, &r->attr) : -EROFS;
      break;
    case UFS_READLINK:
      n = ops->readlink ? ops->readlink(ctx, q->ino, (char *)data, UFS_MAX_IO) : -EINVAL;
      if (n < 0)
        err = (int)n;
      else
        r->len = (uint32_t)n;
      break;
    case UFS_SYMLINK:
    case UFS_LINK:
      err = -EPERM;
      break;
    case UFS_STATFS:
      err = ops->statfs(ctx, &r->st);
      break;
    case UFS_SYNC:
      err = ops->sync ? ops->sync(ctx) : 0;
      break;
    default:
      err = -ENOSYS;
  }
  r->err = err;
  if (err) r->len = 0;
}

int ufs_serve(const char *source, const char *mountpoint, const struct ufs_ops *ops, void *ctx, bool readonly) {
  struct sigaction sa = {0};
  sa.sa_handler = on_term;
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  signal(SIGPIPE, SIG_IGN);

  int ch[2];
  if (olux_channel(ch, O_CLOEXEC) < 0) {
    ufs_log("ufs: cannot create channel: %s", strerror(errno));
    return 1;
  }
  /* greeting: the kernel reads the root's attributes during mount() */
  struct ufs_reply hello = {0};
  int err = ops->root(ctx, &hello.attr);
  if (err || olux_msg_send(ch[0], &hello, sizeof(hello), NULL, 0) < 0) {
    ufs_log("ufs: %s: cannot describe the root: %s", source, strerror(err ? -err : errno));
    return 1;
  }
  char opts[64];
  bool attach = is_userfs_mount(mountpoint);
  snprintf(opts, sizeof(opts), "fd=%d%s", ch[1], attach ? ",attach" : "");
  if (mount(source, mountpoint, "userfs", readonly ? MS_RDONLY : 0, opts) < 0) {
    ufs_log("ufs: mount %s on %s failed: %s", source, mountpoint, strerror(errno));
    return 1;
  }
  close(ch[1]); /* the kernel holds its own reference */
  ufs_log("ufs: %s %s on %s (%s)", attach ? "re-attached" : "mounted", source, mountpoint, readonly ? "ro" : "rw");

  size_t qcap = sizeof(struct ufs_req) + UFS_MAX_IO, rcap = sizeof(struct ufs_reply) + UFS_MAX_IO;
  struct ufs_req *q = malloc(qcap);
  struct ufs_reply *r = malloc(rcap);
  if (!q || !r) return 1;
  for (;;) {
    if (stop_requested) {
      finish(ops, ctx);
      ufs_log("ufs: %s: terminated, filesystem synced", source);
      return 0;
    }
    struct olux_msg_info info;
    ssize_t n = olux_msg_recv(ch[0], q, qcap, &info, NULL);
    if (n < 0) {
      if (errno == EINTR) continue;
      ufs_log("ufs: %s: receive failed: %s", source, strerror(errno));
      finish(ops, ctx);
      return 1;
    }
    if (n == 0) { /* kernel side closed: unmounted */
      finish(ops, ctx);
      return 0;
    }
    if (info.sender_pid != 0 || (size_t)n < sizeof(*q)) continue; /* only the kernel may talk to us */
    memset(r, 0, sizeof(*r));
    r->id = q->id;
    handle(ops, ctx, q, (size_t)n, r);
    if (olux_msg_send(ch[0], r, sizeof(*r) + r->len, NULL, 0) < 0 && errno == EPIPE) {
      finish(ops, ctx);
      return 0;
    }
  }
}
