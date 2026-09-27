/*
 * userfs protocol: the kernel forwards VFS operations on a mounted
 * filesystem to a userspace server over an IPC channel.
 *
 * Mount:  mount(source, dir, "userfs", flags, "fd=<channel fd>[,attach]")
 *   The kernel keeps the given endpoint; the server serves the other end.
 *   Before calling mount() the server queues a greeting on its end: a
 *   `struct ufs_reply` with id 0 and err 0 whose attr describes the root
 *   (the kernel reads it during mount instead of sending a request, since
 *   the server is blocked in mount() and cannot answer).
 *   "attach" re-binds an existing userfs mount on `dir` whose server died
 *   (used when init restarts a crashed server). Inode numbers must be
 *   stable across server restarts.
 *
 * Every request is `struct ufs_req` (+ write data); every reply is
 * `struct ufs_reply` (+ read/readdir/readlink data). Replies carry the
 * request id; errors are negative errno values in `err`.
 */
#ifndef UAPI_OLUX_USERFS_H
#define UAPI_OLUX_USERFS_H

#include <stdint.h>

enum ufs_op {
  UFS_HELLO = 1, /* reply.attr describes the root directory (also the greeting) */
  UFS_LOOKUP,    /* ino=dir, name -> attr */
  UFS_GETATTR,   /* ino -> attr */
  UFS_READ,      /* ino, off, len -> data */
  UFS_WRITE,     /* ino, off, len + data -> len written, attr */
  UFS_READDIR,   /* ino, off(cookie), len -> entries */
  UFS_CREATE,    /* ino=dir, name, mode, rdev -> attr */
  UFS_MKDIR,     /* ino=dir, name, mode -> attr */
  UFS_UNLINK,    /* ino=dir, name */
  UFS_RMDIR,     /* ino=dir, name */
  UFS_RENAME,    /* ino=olddir, name, ino2=newdir, name2 */
  UFS_SETATTR,   /* ino, valid, mode/uid/gid/size/atime/mtime -> attr */
  UFS_READLINK,  /* ino -> data */
  UFS_SYMLINK,   /* ino=dir, name, name2=target -> attr */
  UFS_LINK,      /* ino=dir, name, ino2=target */
  UFS_STATFS,    /* -> statfs */
  UFS_SYNC,      /* flush everything to stable storage */
};

#define UFS_NAME_MAX 255

/* setattr `valid` bits (same as the kernel's ATTR_*) */
#define UFS_ATTR_MODE 1
#define UFS_ATTR_UID 2
#define UFS_ATTR_GID 4
#define UFS_ATTR_SIZE 8
#define UFS_ATTR_ATIME 16
#define UFS_ATTR_MTIME 32

struct ufs_attr {
  uint64_t ino;
  uint32_t mode, nlink, uid, gid;
  uint64_t size, blocks;
  int64_t atime, mtime, ctime; /* seconds */
  uint64_t rdev;
};

struct ufs_req {
  uint32_t op;
  uint32_t id;
  uint64_t ino, ino2;
  uint64_t off;
  uint32_t len;
  uint32_t mode;
  uint32_t uid, gid;
  uint64_t size, rdev;
  int64_t atime, mtime;
  uint32_t valid;
  uint32_t pad;
  char name[UFS_NAME_MAX + 1];
  char name2[1024]; /* rename target name or symlink target */
  /* UFS_WRITE: `len` bytes of data follow */
};

struct ufs_statfs {
  uint64_t blocks, bfree, files, ffree;
  uint32_t bsize, namelen;
};

struct ufs_reply {
  int32_t err;
  uint32_t id;
  uint32_t len;   /* bytes of data following the reply */
  uint32_t pad;
  uint64_t count; /* UFS_WRITE: bytes written */
  struct ufs_attr attr;
  struct ufs_statfs st;
};

/* UFS_READDIR data: packed records, 8-byte aligned */
struct ufs_dirent {
  uint64_t ino;
  uint64_t next;  /* cookie to continue after this entry */
  uint16_t reclen;
  uint8_t type;   /* DT_* */
  uint8_t namelen;
  char name[];
};

#define UFS_MAX_IO (32 * 1024) /* largest read/write payload per request */

#endif
