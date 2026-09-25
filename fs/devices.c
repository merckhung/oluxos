/* Character device registry and /dev node management. */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>

static struct cdev *cdevs;

struct pending_node {
  char name[32];
  mode_t mode;
  dev_t dev;
  struct pending_node *next;
};
static struct pending_node *pending; /* devices registered before /dev exists */
static bool devfs_ready;

int register_chrdev(dev_t dev, const char *name, const struct file_operations *fops, void *priv) {
  if (cdev_lookup(dev)) return -EBUSY;
  struct cdev *c = kzalloc(sizeof(*c), 0);
  if (!c) return -ENOMEM;
  c->dev = dev;
  c->name = name;
  c->fops = fops;
  c->priv = priv;
  c->next = cdevs;
  cdevs = c;
  return 0;
}

struct cdev *cdev_lookup(dev_t dev) {
  for (struct cdev *c = cdevs; c; c = c->next)
    if (c->dev == dev) return c;
  return NULL;
}

static int make_node(const char *name, mode_t mode, dev_t dev) {
  char path[48];
  snprintf(path, sizeof(path), "/dev/%s", name);
  /* create intermediate directories, e.g. /dev/input/event0 */
  for (char *s = path + 5; (s = strchr(s, '/')); s++) {
    *s = '\0';
    vfs_mkdir(AT_FDCWD, path, 0755);
    *s = '/';
  }
  int r = vfs_mknod(AT_FDCWD, path, mode, dev);
  if (r == 0) {
    struct path p;
    if (!kern_path(path, 0, &p)) {
      p.dentry->inode->mode = mode; /* ignore umask for device nodes */
      path_put(&p);
    }
  }
  return r == -EEXIST ? 0 : r;
}

int devfs_create(const char *name, mode_t mode, dev_t dev) {
  if (devfs_ready) return make_node(name, mode, dev);
  struct pending_node *n = kzalloc(sizeof(*n), 0);
  if (!n) return -ENOMEM;
  strlcpy(n->name, name, sizeof(n->name));
  n->mode = mode;
  n->dev = dev;
  n->next = pending;
  pending = n;
  return 0;
}

/* Called once /dev is mounted: create all nodes registered so far. */
void devfs_mount_all(void) {
  devfs_ready = true;
  /* reverse to keep registration order */
  struct pending_node *rev = NULL;
  while (pending) {
    struct pending_node *n = pending;
    pending = n->next;
    n->next = rev;
    rev = n;
  }
  while (rev) {
    struct pending_node *n = rev;
    rev = n->next;
    make_node(n->name, n->mode, n->dev);
    kfree(n);
  }
  vfs_symlink("/proc/self/fd", AT_FDCWD, "/dev/fd");
  vfs_symlink("/proc/self/fd/0", AT_FDCWD, "/dev/stdin");
  vfs_symlink("/proc/self/fd/1", AT_FDCWD, "/dev/stdout");
  vfs_symlink("/proc/self/fd/2", AT_FDCWD, "/dev/stderr");
  vfs_mkdir(AT_FDCWD, "/dev/pts", 0755);
  vfs_mkdir(AT_FDCWD, "/dev/shm", 01777);
}
