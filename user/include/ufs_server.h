/* Framework for userfs filesystem servers (see include/uapi/olux/userfs.h). */
#ifndef UFS_SERVER_H
#define UFS_SERVER_H

#include <olux/userfs.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Exit status telling init not to restart the server (wrong fs, no device). */
#define UFS_EXIT_NOT_APPLICABLE 78

/* Returns false when the reply buffer is full. */
typedef bool (*ufs_filler)(void *fctx, uint64_t ino, uint64_t next_cookie, uint8_t type, const char *name,
                           size_t namelen);

struct ufs_ops {
  int (*root)(void *ctx, struct ufs_attr *a);
  int (*lookup)(void *ctx, uint64_t dir, const char *name, struct ufs_attr *a);
  int (*getattr)(void *ctx, uint64_t ino, struct ufs_attr *a);
  ssize_t (*read)(void *ctx, uint64_t ino, uint64_t off, void *buf, size_t len, struct ufs_attr *a);
  ssize_t (*write)(void *ctx, uint64_t ino, uint64_t off, const void *buf, size_t len, struct ufs_attr *a);
  int (*readdir)(void *ctx, uint64_t ino, uint64_t cookie, ufs_filler fill, void *fctx);
  int (*create)(void *ctx, uint64_t dir, const char *name, uint32_t mode, struct ufs_attr *a);
  int (*mkdir)(void *ctx, uint64_t dir, const char *name, uint32_t mode, struct ufs_attr *a);
  int (*unlink)(void *ctx, uint64_t dir, const char *name);
  int (*rmdir)(void *ctx, uint64_t dir, const char *name);
  int (*rename)(void *ctx, uint64_t odir, const char *oname, uint64_t ndir, const char *nname);
  int (*setattr)(void *ctx, uint64_t ino, const struct ufs_req *req, struct ufs_attr *a);
  ssize_t (*readlink)(void *ctx, uint64_t ino, char *buf, size_t size);
  int (*statfs)(void *ctx, struct ufs_statfs *st);
  int (*sync)(void *ctx);
  /* Called once before the server exits (unmount or SIGTERM); defaults to sync. */
  void (*shutdown)(void *ctx);
};

/* Mount (or re-attach to) `mountpoint` and serve requests until unmounted
 * or terminated. Returns the process exit status. */
int ufs_serve(const char *source, const char *mountpoint, const struct ufs_ops *ops, void *ctx, bool readonly);

void ufs_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
