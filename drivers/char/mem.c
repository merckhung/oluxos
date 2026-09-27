/* /dev/null, /dev/zero, /dev/full and /dev/kmsg. */
#include <olux/device.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>

static ssize_t null_read(struct file *f, struct iobuf *b, loff_t *pos) { return 0; }
static ssize_t null_write(struct file *f, struct iobuf *b, loff_t *pos) { return b->len; }

static ssize_t zero_read(struct file *f, struct iobuf *b, loff_t *pos) {
  static const u8 zeros[512];
  size_t done = 0;
  while (done < b->len) {
    size_t c = MIN(b->len - done, sizeof(zeros));
    if (iob_write(b, done, zeros, c)) return done ? (ssize_t)done : -EFAULT;
    done += c;
  }
  return done;
}

static ssize_t full_write(struct file *f, struct iobuf *b, loff_t *pos) { return -ENOSPC; }
static loff_t null_llseek(struct file *f, loff_t off, int whence) { return f->pos = 0; }
static unsigned always_ready(struct file *f, struct poll_table *pt) { return POLLIN | POLLOUT; }

static const struct file_operations null_fops = {
    .read = null_read, .write = null_write, .llseek = null_llseek, .poll = always_ready};
static const struct file_operations zero_fops = {
    .read = zero_read, .write = null_write, .llseek = null_llseek, .poll = always_ready};
static const struct file_operations full_fops = {
    .read = zero_read, .write = full_write, .llseek = null_llseek, .poll = always_ready};

/* /dev/kmsg: read the kernel log; writes are logged. */
struct kmsg_state {
  size_t pos;
};

static int kmsg_open(struct inode *i, struct file *f) {
  struct kmsg_state *s = kzalloc(sizeof(*s), 0);
  if (!s) return -ENOMEM;
  f->priv = s;
  return 0;
}

static int kmsg_release(struct inode *i, struct file *f) {
  kfree(f->priv);
  return 0;
}

static ssize_t kmsg_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct kmsg_state *s = f->priv;
  char tmp[256];
  size_t done = 0;
  while (done < b->len) {
    size_t n = klog_read(tmp, MIN(sizeof(tmp), b->len - done), &s->pos);
    if (!n) break;
    if (iob_write(b, done, tmp, n)) return -EFAULT;
    done += n;
  }
  return done;
}

static ssize_t kmsg_write(struct file *f, struct iobuf *b, loff_t *pos) {
  char tmp[256];
  size_t n = MIN(b->len, sizeof(tmp) - 1);
  if (iob_read(b, 0, tmp, n)) return -EFAULT;
  tmp[n] = '\0';
  const char *msg = tmp;
  int level = LOGLEVEL_INFO;
  if (msg[0] == '<' && isdigit(msg[1]) && msg[2] == '>') {
    level = msg[1] - '0';
    msg += 3;
  }
  printk(level, "%s%s", msg, (n && tmp[n - 1] == '\n') ? "" : "\n");
  return b->len;
}

static const struct file_operations kmsg_fops = {
    .open = kmsg_open, .release = kmsg_release, .read = kmsg_read, .write = kmsg_write};

static int mem_devices(void) {
  register_chrdev(MKDEV(1, 3), "null", &null_fops, NULL);
  register_chrdev(MKDEV(1, 5), "zero", &zero_fops, NULL);
  register_chrdev(MKDEV(1, 7), "full", &full_fops, NULL);
  register_chrdev(MKDEV(1, 11), "kmsg", &kmsg_fops, NULL);
  devfs_create("null", S_IFCHR | 0666, MKDEV(1, 3));
  devfs_create("zero", S_IFCHR | 0666, MKDEV(1, 5));
  devfs_create("full", S_IFCHR | 0666, MKDEV(1, 7));
  devfs_create("kmsg", S_IFCHR | 0644, MKDEV(1, 11));
  return 0;
}
core_initcall(mem_devices);
