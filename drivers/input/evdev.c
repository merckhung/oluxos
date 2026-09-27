/*
 * Input core and /dev/input/eventN (Linux evdev ABI): each open file has
 * its own event queue; EVIOCGVERSION/ID/NAME/BIT and EVIOCGRAB are
 * supported.
 */
#include <olux/device.h>
#include <olux/fs.h>
#include <olux/input.h>
#include <olux/kernel.h>
#include <olux/list.h>
#include <olux/mm.h>
#include <olux/sched.h>
#include <olux/spinlock.h>
#include <olux/time.h>
#include <olux/uaccess.h>
#include <olux/wait.h>

#define MAX_INPUT 32
#define QLEN 256

struct input_event_abi {
  s64 sec, usec;
  u16 type, code;
  s32 value;
};

struct input_dev {
  char name[64];
  u16 bustype, vendor, product;
  u32 evbits;
  u64 keybits[(KEY_MAX + 64) / 64];
  u64 relbits;
  int minor;
  bool gone;
  spinlock_t lock;
  struct list_head clients;
  struct file *grab;
  struct wait_queue wq;
};

struct client {
  struct input_dev *dev;
  struct input_event_abi q[QLEN];
  unsigned head, tail;
  struct list_head link;
  struct file *file;
};

static struct input_dev *devs[MAX_INPUT];

struct input_dev *input_register(const char *name, u16 bustype, u16 vendor, u16 product, u32 evbits) {
  int minor = -1;
  for (int i = 0; i < MAX_INPUT; i++)
    if (!devs[i]) {
      minor = i;
      break;
    }
  if (minor < 0) return NULL;
  struct input_dev *d = kzalloc(sizeof(*d), 0);
  if (!d) return NULL;
  strlcpy(d->name, name, sizeof(d->name));
  d->bustype = bustype;
  d->vendor = vendor;
  d->product = product;
  d->evbits = evbits | 1u << EV_SYN;
  d->minor = minor;
  spin_lock_init(&d->lock);
  list_init(&d->clients);
  wq_init(&d->wq);
  devs[minor] = d;
  char node[24];
  snprintf(node, sizeof(node), "input/event%d", minor);
  devfs_create(node, S_IFCHR | 0640, MKDEV(13, 64 + minor));
  pr_info("input: %s as /dev/%s\n", d->name, node);
  return d;
}

void input_set_key(struct input_dev *d, unsigned code) {
  if (code <= KEY_MAX) d->keybits[code / 64] |= 1ULL << (code % 64);
}

void input_set_rel(struct input_dev *d, unsigned code) {
  if (code < 64) d->relbits |= 1ULL << code;
}

void input_event(struct input_dev *d, unsigned type, unsigned code, int value) {
  if (!d) return;
  u64 now = ktime_realtime_ns();
  struct input_event_abi e = {(s64)(now / NSEC_PER_SEC), (s64)(now % NSEC_PER_SEC / 1000), (u16)type, (u16)code, value};
  unsigned long f = spin_lock_irqsave(&d->lock);
  struct client *c;
  list_for_each_entry(c, &d->clients, link) {
    if (d->grab && d->grab != c->file) continue;
    if (c->head - c->tail < QLEN) c->q[c->head++ % QLEN] = e;
  }
  spin_unlock_irqrestore(&d->lock, f);
  if (type == EV_SYN) wake_up(&d->wq);
}

void input_unregister(struct input_dev *d) {
  if (!d) return;
  d->gone = true;
  wake_up(&d->wq);
  char node[24];
  snprintf(node, sizeof(node), "input/event%d", d->minor);
  devfs_remove(node);
  devs[d->minor] = NULL; /* the structure is kept: open files may still refer to it */
}

/* ---------------- /dev/input/eventN ---------------- */

static int evdev_open(struct inode *i, struct file *f) {
  unsigned minor = MINOR(i->rdev) - 64;
  if (minor >= MAX_INPUT || !devs[minor]) return -ENODEV;
  struct client *c = kzalloc(sizeof(*c), 0);
  if (!c) return -ENOMEM;
  c->dev = devs[minor];
  c->file = f;
  unsigned long fl = spin_lock_irqsave(&c->dev->lock);
  list_add_tail(&c->link, &c->dev->clients);
  spin_unlock_irqrestore(&c->dev->lock, fl);
  f->priv = c;
  return 0;
}

static int evdev_release(struct inode *i, struct file *f) {
  struct client *c = f->priv;
  unsigned long fl = spin_lock_irqsave(&c->dev->lock);
  list_del(&c->link);
  if (c->dev->grab == f) c->dev->grab = NULL;
  spin_unlock_irqrestore(&c->dev->lock, fl);
  kfree(c);
  return 0;
}

static ssize_t evdev_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct client *c = f->priv;
  struct input_dev *d = c->dev;
  if (b->len < sizeof(struct input_event_abi)) return -EINVAL;
  if (c->head == c->tail) {
    if (d->gone) return -ENODEV;
    if (f->flags & O_NONBLOCK) return -EAGAIN;
    int r = wait_event_interruptible(d->wq, c->head != c->tail || d->gone);
    if (r) return r;
    if (c->head == c->tail) return -ENODEV;
  }
  size_t done = 0;
  while (done + sizeof(struct input_event_abi) <= b->len) {
    unsigned long fl = spin_lock_irqsave(&d->lock);
    if (c->head == c->tail) {
      spin_unlock_irqrestore(&d->lock, fl);
      break;
    }
    struct input_event_abi e = c->q[c->tail++ % QLEN];
    spin_unlock_irqrestore(&d->lock, fl);
    if (iob_write(b, done, &e, sizeof(e))) return done ? (ssize_t)done : -EFAULT;
    done += sizeof(e);
  }
  return done;
}

static unsigned evdev_poll(struct file *f, struct poll_table *pt) {
  struct client *c = f->priv;
  poll_wait(f, &c->dev->wq, pt);
  unsigned m = c->head != c->tail ? POLLIN | POLLRDNORM : 0;
  if (c->dev->gone) m |= POLLHUP | POLLERR;
  return m;
}

#define IOC_NR(c) ((c) & 0xff)
#define IOC_TYPE(c) (((c) >> 8) & 0xff)
#define IOC_SIZE(c) (((c) >> 16) & 0x3fff)
#define IOC_DIR(c) ((c) >> 30)

static long put_bits(u64 arg, const void *bits, size_t have, size_t len) {
  u8 tmp[(KEY_MAX + 64) / 8] = {0};
  size_t n = MIN(have, len);
  if (bits && n) memcpy(tmp, bits, n);
  if (copy_to_user(arg, tmp, MIN(len, sizeof(tmp)))) return -EFAULT;
  return (long)MIN(len, sizeof(tmp));
}

static long evdev_ioctl(struct file *f, unsigned cmd, u64 arg) {
  struct client *c = f->priv;
  struct input_dev *d = c->dev;
  if (IOC_TYPE(cmd) != 'E') return -ENOTTY;
  unsigned nr = IOC_NR(cmd), size = IOC_SIZE(cmd);
  if (cmd == 0x80044501) return put_user(0x010001, (int *)arg); /* EVIOCGVERSION */
  if (cmd == 0x80084502) {                                      /* EVIOCGID */
    u16 id[4] = {d->bustype, d->vendor, d->product, 1};
    return copy_to_user(arg, id, sizeof(id));
  }
  if (cmd == 0x40044590) { /* EVIOCGRAB */
    unsigned long fl = spin_lock_irqsave(&d->lock);
    long r = 0;
    if (arg) {
      if (d->grab && d->grab != f)
        r = -EBUSY;
      else
        d->grab = f;
    } else {
      if (d->grab != f)
        r = -EINVAL;
      else
        d->grab = NULL;
    }
    spin_unlock_irqrestore(&d->lock, fl);
    return r;
  }
  if (IOC_DIR(cmd) == 2 && nr == 0x06) { /* EVIOCGNAME(len) */
    size_t n = MIN((size_t)size, strlen(d->name) + 1);
    return copy_to_user(arg, d->name, n) ? -EFAULT : (long)n;
  }
  if (IOC_DIR(cmd) == 2 && (nr == 0x07 || nr == 0x08)) /* EVIOCGPHYS / EVIOCGUNIQ */
    return size ? (put_user((u8)0, (u8 *)arg) ? -EFAULT : 1) : 0;
  if (IOC_DIR(cmd) == 2 && nr >= 0x20 && nr < 0x40) { /* EVIOCGBIT(ev, len) */
    unsigned ev = nr - 0x20;
    if (ev == 0) return put_bits(arg, &d->evbits, sizeof(d->evbits), size);
    if (ev == EV_KEY) return put_bits(arg, d->keybits, sizeof(d->keybits), size);
    if (ev == EV_REL) return put_bits(arg, &d->relbits, sizeof(d->relbits), size);
    return put_bits(arg, NULL, 0, size);
  }
  if (IOC_DIR(cmd) == 2 && (nr == 0x18 || nr == 0x19 || nr == 0x1a || nr == 0x1b)) /* key/led/snd/sw state */
    return put_bits(arg, NULL, 0, size);
  return -ENOTTY;
}

static const struct file_operations evdev_fops = {
    .open = evdev_open, .release = evdev_release, .read = evdev_read, .poll = evdev_poll, .ioctl = evdev_ioctl};

static int evdev_init(void) {
  for (int i = 0; i < MAX_INPUT; i++) register_chrdev(MKDEV(13, 64 + i), "input", &evdev_fops, NULL);
  return 0;
}
core_initcall(evdev_init);
