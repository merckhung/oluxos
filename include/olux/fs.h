#ifndef OLUX_FS_H
#define OLUX_FS_H

#include <olux/list.h>
#include <olux/spinlock.h>
#include <olux/time.h>
#include <olux/types.h>
#include <olux/wait.h>

/* ---- mode bits ---- */
#define S_IFMT 0170000
#define S_IFSOCK 0140000
#define S_IFLNK 0120000
#define S_IFREG 0100000
#define S_IFBLK 0060000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* ---- open flags (arm64 Linux values) ---- */
#define O_ACCMODE 03
#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DSYNC 010000
#define O_ASYNC 020000
#define O_DIRECTORY 040000
#define O_NOFOLLOW 0100000
#define O_DIRECT 0200000
#define O_LARGEFILE 0400000
#define O_NOATIME 01000000
#define O_CLOEXEC 02000000
#define O_SYNC 04010000
#define O_PATH 010000000
#define O_TMPFILE 020040000

#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define AT_EACCESS 0x200
#define AT_SYMLINK_FOLLOW 0x400
#define AT_NO_AUTOMOUNT 0x800
#define AT_EMPTY_PATH 0x1000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

#define PATH_MAX 4096
#define NAME_MAX 255
#define MAX_SYMLINKS 40

#define MKDEV(ma, mi) (((dev_t)(ma) << 8) | ((mi) & 0xff) | (((dev_t)(mi) & ~0xffUL) << 12))
#define MAJOR(d) ((u32)(((d) >> 8) & 0xfff))
#define MINOR(d) ((u32)(((d) & 0xff) | (((d) >> 12) & 0xfff00)))

/* ---- poll ---- */
#define POLLIN 0x001
#define POLLRDHUP 0x2000
#define POLLPRI 0x002
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define POLLNVAL 0x020
#define POLLRDNORM 0x040
#define POLLRDBAND 0x080
#define POLLWRNORM 0x100
#define POLLWRBAND 0x200

struct inode;
struct file;
struct dentry;
struct super_block;
struct mount;
struct vma;

/* A buffer that is either in user or kernel space. */
struct iobuf {
  u8 *ptr;
  size_t len;
  bool user;
};
static inline struct iobuf kbuf(void *p, size_t n) { return (struct iobuf){p, n, false}; }
static inline struct iobuf ubuf(u64 p, size_t n) { return (struct iobuf){(u8 *)p, n, true}; }
/* Returns 0 or -EFAULT. */
int iob_write(struct iobuf *b, size_t off, const void *src, size_t n); /* into buffer */
int iob_read(struct iobuf *b, size_t off, void *dst, size_t n);        /* from buffer */

struct path {
  struct mount *mnt;
  struct dentry *dentry;
};

struct iattr {
  u32 valid;
  mode_t mode;
  uid_t uid;
  gid_t gid;
  loff_t size;
  struct timespec64 atime, mtime;
};
#define ATTR_MODE 1
#define ATTR_UID 2
#define ATTR_GID 4
#define ATTR_SIZE 8
#define ATTR_ATIME 16
#define ATTR_MTIME 32

struct inode_operations {
  int (*lookup)(struct inode *dir, const char *name, struct inode **out);
  int (*create)(struct inode *dir, const char *name, mode_t mode, dev_t rdev, struct inode **out);
  int (*mkdir)(struct inode *dir, const char *name, mode_t mode, struct inode **out);
  int (*unlink)(struct inode *dir, const char *name, struct inode *victim);
  int (*rmdir)(struct inode *dir, const char *name, struct inode *victim);
  int (*rename)(struct inode *odir, const char *oname, struct inode *ndir, const char *nname,
                struct inode *victim, struct inode *target);
  int (*link)(struct inode *dir, const char *name, struct inode *target);
  int (*symlink)(struct inode *dir, const char *name, const char *target, struct inode **out);
  ssize_t (*readlink)(struct inode *inode, char *buf, size_t size);
  int (*setattr)(struct inode *inode, const struct iattr *attr);
};

struct dir_context;
typedef bool (*filldir_t)(struct dir_context *ctx, const char *name, size_t len, ino_t ino, unsigned type);
struct dir_context {
  filldir_t actor;
  loff_t pos; /* opaque cookie managed by the filesystem */
};

struct poll_table;

struct file_operations {
  int (*open)(struct inode *inode, struct file *f);
  int (*release)(struct inode *inode, struct file *f);
  ssize_t (*read)(struct file *f, struct iobuf *buf, loff_t *pos);
  ssize_t (*write)(struct file *f, struct iobuf *buf, loff_t *pos);
  loff_t (*llseek)(struct file *f, loff_t off, int whence);
  int (*iterate)(struct file *f, struct dir_context *ctx);
  unsigned (*poll)(struct file *f, struct poll_table *pt);
  long (*ioctl)(struct file *f, unsigned cmd, u64 arg);
  int (*mmap)(struct file *f, struct vma *vma, u64 off);
  int (*fsync)(struct file *f);
};

struct super_operations {
  void (*evict)(struct inode *inode); /* last reference to an unlinked inode */
  int (*statfs)(struct super_block *sb, u64 *blocks, u64 *bfree, u64 *files, u64 *ffree, u32 *bsize);
  void (*put_super)(struct super_block *sb);
  int (*sync)(struct super_block *sb);
};

struct inode {
  ino_t ino;
  mode_t mode;
  u32 nlink;
  uid_t uid;
  gid_t gid;
  loff_t size;
  dev_t rdev;
  u64 blocks;
  struct timespec64 atime, mtime, ctime;
  struct super_block *sb;
  const struct inode_operations *i_op;
  const struct file_operations *f_op;
  void *priv;
  atomic_t refcount;
  struct pipe *pipe;
  struct list_head sb_link;
};

struct dentry {
  char *name;
  struct dentry *parent;
  struct inode *inode;
  struct list_head children;
  struct list_head sibling;
  atomic_t refcount;
  struct mount *mounted; /* a filesystem is mounted on this dentry */
  bool dead;
};

#define SB_RDONLY 1
#define SB_NOEXEC 2
#define SB_NOSUID 4
#define SB_NODEV 8

struct super_block {
  const struct fs_type *type;
  struct dentry *root;
  const struct super_operations *s_op;
  void *priv;
  dev_t dev;
  u32 flags;
  ino_t next_ino;
  struct list_head inodes;
};

struct mount {
  struct super_block *sb;
  struct dentry *mountpoint; /* in parent */
  struct mount *parent;
  struct list_head link;
  char *devname;
  u32 flags;
};

struct fs_type {
  const char *name;
  int (*mount)(struct super_block *sb, const char *dev, const char *data);
  bool nodev;
  struct fs_type *next;
};

#define FMODE_READ 1
#define FMODE_WRITE 2

struct file {
  atomic_t refcount;
  const struct file_operations *f_op;
  struct inode *inode;
  struct path path;
  loff_t pos;
  u32 flags;  /* O_* status flags */
  u32 mode;   /* FMODE_* */
  void *priv;
  struct mutex pos_lock;
};

/* ---- VFS API ---- */
void vfs_init(void);
int register_filesystem(struct fs_type *fs);
struct inode *new_inode(struct super_block *sb, mode_t mode);
void inode_get(struct inode *i);
void inode_put(struct inode *i);
struct dentry *d_alloc(struct dentry *parent, const char *name, struct inode *inode);
void d_get(struct dentry *d);
void d_put(struct dentry *d);
void path_get(struct path *p);
void path_put(struct path *p);
struct timespec64 current_time(void);
unsigned mode_to_dtype(mode_t m);

#define LOOKUP_FOLLOW 1
#define LOOKUP_DIRECTORY 2
#define LOOKUP_PARENT 4
/* Resolve `name` relative to dirfd. On success fills *out (referenced). */
int path_lookupat(int dirfd, const char *name, unsigned flags, struct path *out);
int kern_path(const char *name, unsigned flags, struct path *out);
/* Parent directory + last component (for create/unlink/rename). */
int path_parentat(int dirfd, const char *name, struct path *parent, char *last);
int d_path(const struct path *p, char *buf, size_t size);

struct file *file_alloc(void);
void file_get(struct file *f);
void file_put(struct file *f);
struct file *vfs_open(int dirfd, const char *name, int flags, mode_t mode, int *err);
struct file *kernel_open(const char *name, int flags, mode_t mode);
struct file *open_path(struct path *p, int flags, int *err);
ssize_t vfs_read(struct file *f, struct iobuf *b, loff_t *pos);
ssize_t vfs_write(struct file *f, struct iobuf *b, loff_t *pos);
ssize_t kernel_pread(struct file *f, void *buf, size_t n, loff_t pos);
ssize_t kernel_pwrite(struct file *f, const void *buf, size_t n, loff_t pos);
int vfs_mknod(int dirfd, const char *name, mode_t mode, dev_t dev);
int vfs_mkdir(int dirfd, const char *name, mode_t mode);
int vfs_unlink(int dirfd, const char *name);
int vfs_rmdir(int dirfd, const char *name);
int vfs_rename(int olddirfd, const char *old, int newdirfd, const char *newn);
int vfs_link(int olddirfd, const char *old, int newdirfd, const char *newn, int flags);
int vfs_symlink(const char *target, int dirfd, const char *name);
int vfs_setattr(struct inode *inode, struct iattr *a);
int vfs_truncate(struct inode *inode, loff_t len);
int do_mount(const char *dev, const char *dir, const char *type, u32 flags, const char *data);
int do_umount(const char *dir, int flags);
int vfs_statfs(struct super_block *sb, void *kstatfs);
void vfs_sync_all(void);
struct mount *root_mount(void);
int mounts_show(char *buf, size_t size);
int permission(struct inode *inode, int mask); /* mask: 4 r, 2 w, 1 x */

/* character / block device registry */
struct cdev {
  dev_t dev;
  const char *name;
  const struct file_operations *fops;
  void *priv;
  struct cdev *next;
};
int register_chrdev(dev_t dev, const char *name, const struct file_operations *fops, void *priv);
struct cdev *cdev_lookup(dev_t dev);
void unregister_chrdev(dev_t dev);
int devfs_remove(const char *name);
/* Create a node in /dev for a registered device. */
int devfs_create(const char *name, mode_t mode, dev_t dev);
void devfs_mount_all(void);

/* poll support */
struct poll_entry {
  struct waiter w;
  struct wait_queue *wq;
};
#define POLL_MAX_ENTRIES 64
struct poll_table {
  int n;
  bool overflow;
  struct poll_entry entries[POLL_MAX_ENTRIES];
};
void poll_wait(struct file *f, struct wait_queue *wq, struct poll_table *pt);

/* fd table */
#define NR_OPEN_MAX 1024
struct fdtable {
  atomic_t refcount;
  int max;
  struct file **fd;
  u64 *cloexec; /* bitmap */
};
struct fdtable *fdtable_alloc(int max);
struct fdtable *fdtable_dup(struct fdtable *t);
void fdtable_put(struct fdtable *t);
void fdtable_close_on_exec(struct fdtable *t);
struct file *fget(int fd); /* referenced */

/* /proc/net/<name>: `show` prints the file's contents with pr(ctx, ...). */
typedef void (*seq_printf_t)(void *ctx, const char *fmt, ...) __printf(2, 3);
void proc_net_register(const char *name, void (*show)(seq_printf_t pr, void *ctx));
int fd_install(struct file *f, int min_fd, bool cloexec);
int fd_close(int fd);

/* pipes */
struct file *pipe_create_pair(struct file **wr, int flags);
struct pipe;
int fifo_open(struct inode *inode, struct file *f);

/* initramfs */
int unpack_initramfs(const void *data, size_t size);

#endif
