/*
 * Unix98 pseudo-terminals: /dev/ptmx hands out a master; the slave appears
 * as /dev/pts/N and is a normal tty (line discipline, job control). Output
 * written to the slave is buffered for the master's reader, with flow
 * control; data written to the master is the slave's keyboard input.
 * Closing the master hangs up the slave's session.
 */
#include <olux/device.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/tty.h>
#include <olux/uaccess.h>

#define NPTY 64
#define OBUF 16384
#define PTS_MAJOR 136

#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCPKT 0x5420
#define FIONREAD 0x541b
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TIOCGPTN 0x80045430u
#define TIOCSPTLCK 0x40045431u
#define TIOCGPTLCK 0x80045439u

struct pty {
  int index;
  struct tty *slave;
  char obuf[OBUF];
  unsigned ohead, otail;
  struct wait_queue mwait;
  bool master_open, locked, slave_opened;
  char name[16];
};

static struct pty *ptys[NPTY];
static struct file_operations pts_fops;

static unsigned out_count(struct pty *p) { return p->ohead - p->otail; }

/* slave output (tty lock held) */
static void pty_write(struct tty *t, const char *s, size_t n) {
  struct pty *p = t->priv;
  for (size_t i = 0; i < n && out_count(p) < OBUF; i++) p->obuf[p->ohead++ % OBUF] = s[i];
  wake_up(&p->mwait);
}

static unsigned pty_write_room(struct tty *t) {
  struct pty *p = t->priv;
  return OBUF - out_count(p);
}

static const struct tty_ops pty_ops = {.write = pty_write, .write_room = pty_write_room};

static void pty_destroy(struct pty *p) {
  ptys[p->index] = NULL;
  unregister_chrdev(MKDEV(PTS_MAJOR, p->index));
  tty_free(p->slave);
  kfree(p);
}

/* ---------------- master ---------------- */

static bool slave_gone(struct pty *p) { return p->slave_opened && p->slave->opens == 0; }

static ssize_t ptm_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct pty *p = f->priv;
  struct tty *t = p->slave;
  for (;;) {
    unsigned long fl = spin_lock_irqsave(&t->lock);
    unsigned avail = out_count(p);
    if (avail) {
      char tmp[256];
      size_t done = 0;
      while (done < b->len && out_count(p)) {
        size_t n = 0;
        while (n < sizeof(tmp) && done + n < b->len && out_count(p)) tmp[n++] = p->obuf[p->otail++ % OBUF];
        spin_unlock_irqrestore(&t->lock, fl);
        if (iob_write(b, done, tmp, n)) return done ? (ssize_t)done : -EFAULT;
        done += n;
        fl = spin_lock_irqsave(&t->lock);
      }
      spin_unlock_irqrestore(&t->lock, fl);
      wake_up(&t->write_wait);
      return done;
    }
    spin_unlock_irqrestore(&t->lock, fl);
    if (slave_gone(p)) return -EIO;
    if (f->flags & O_NONBLOCK) return -EAGAIN;
    int r = wait_event_interruptible(p->mwait, out_count(p) || slave_gone(p));
    if (r) return r;
  }
}

static ssize_t ptm_write(struct file *f, struct iobuf *b, loff_t *pos) {
  struct pty *p = f->priv;
  struct tty *t = p->slave;
  size_t done = 0;
  char tmp[256];
  while (done < b->len) {
    unsigned room = tty_input_room(t);
    if (!room) { /* the slave is not reading: wait a little, like a full line */
      if (f->flags & O_NONBLOCK) return done ? (ssize_t)done : -EAGAIN;
      sleep_ns(10 * 1000 * 1000);
      if (signal_pending_current()) return done ? (ssize_t)done : -ERESTARTSYS;
      continue;
    }
    size_t n = MIN(MIN(b->len - done, sizeof(tmp)), (size_t)room);
    if (iob_read(b, done, tmp, n)) return done ? (ssize_t)done : -EFAULT;
    tty_receive(t, tmp, n);
    done += n;
  }
  return done;
}

static unsigned ptm_poll(struct file *f, struct poll_table *pt) {
  struct pty *p = f->priv;
  poll_wait(f, &p->mwait, pt);
  unsigned m = 0;
  if (out_count(p)) m |= POLLIN | POLLRDNORM;
  if (slave_gone(p)) m |= POLLHUP | POLLIN;
  if (tty_input_room(p->slave)) m |= POLLOUT | POLLWRNORM;
  return m;
}

static long ptm_ioctl(struct file *f, unsigned cmd, u64 arg) {
  struct pty *p = f->priv;
  struct tty *t = p->slave;
  int v;
  switch (cmd) {
    case TIOCGPTN:
      return put_user(p->index, (int *)arg);
    case TIOCSPTLCK:
      if (get_user(v, (int *)arg)) return -EFAULT;
      p->locked = v != 0;
      return 0;
    case TIOCGPTLCK:
      return put_user((int)p->locked, (int *)arg);
    case TIOCPKT:
      if (get_user(v, (int *)arg)) return -EFAULT;
      return v ? -EINVAL : 0; /* packet mode is not supported */
    case FIONREAD:
      return put_user((int)out_count(p), (int *)arg);
    case TIOCGWINSZ:
      return copy_to_user(arg, &t->winsize, sizeof(t->winsize));
    case TIOCSWINSZ: {
      struct winsize w;
      if (copy_from_user(&w, arg, sizeof(w))) return -EFAULT;
      bool changed = memcmp(&w, &t->winsize, sizeof(w)) != 0;
      t->winsize = w;
      if (changed && t->pgrp > 0) kill_pgrp(t->pgrp, SIGWINCH, NULL);
      return 0;
    }
    case TCGETS:
    case TCSETS:
    case TCSETSW:
    case TCSETSF: { /* terminal attributes live on the slave */
      struct file tmp = *f;
      tmp.priv = t;
      return tty_generic_fops()->ioctl(&tmp, cmd, arg);
    }
    default:
      return -ENOTTY;
  }
}

static int ptm_release(struct inode *i, struct file *f) {
  struct pty *p = f->priv;
  p->master_open = false;
  devfs_remove(p->name);
  tty_hangup(p->slave);
  if (p->slave->opens == 0) pty_destroy(p);
  return 0;
}

static const struct file_operations ptm_fops = {
    .release = ptm_release, .read = ptm_read, .write = ptm_write, .poll = ptm_poll, .ioctl = ptm_ioctl};

static int ptmx_open(struct inode *i, struct file *f) {
  int idx = -1;
  for (int k = 0; k < NPTY; k++)
    if (!ptys[k]) {
      idx = k;
      break;
    }
  if (idx < 0) return -ENOSPC;
  struct pty *p = kzalloc(sizeof(*p), 0);
  if (!p) return -ENOMEM;
  p->index = idx;
  snprintf(p->name, sizeof(p->name), "pts/%d", idx);
  p->slave = tty_alloc(p->name, &pty_ops, p);
  if (!p->slave) {
    kfree(p);
    return -ENOMEM;
  }
  wq_init(&p->mwait);
  p->locked = true; /* unlockpt() */
  p->master_open = true;
  ptys[idx] = p;
  register_chrdev(MKDEV(PTS_MAJOR, idx), p->name, &pts_fops, p);
  devfs_create(p->name, S_IFCHR | 0620, MKDEV(PTS_MAJOR, idx));
  f->f_op = &ptm_fops;
  f->priv = p;
  return 0;
}

/* ---------------- slave ---------------- */

static int pts_open(struct inode *i, struct file *f) {
  struct pty *p = f->priv; /* the cdev's private data */
  if (!p || !p->master_open) return -EIO;
  if (p->locked) return -EIO;
  p->slave_opened = true;
  return tty_attach(p->slave, f);
}

static int pts_release(struct inode *i, struct file *f) {
  struct tty *t = f->priv;
  struct pty *p = t->priv;
  t->opens--;
  if (t->opens == 0) {
    wake_up(&p->mwait); /* the master's reader sees EIO */
    if (!p->master_open) pty_destroy(p);
  }
  return 0;
}

static const struct file_operations ptmx_fops = {.open = ptmx_open};

static int pty_init(void) {
  pts_fops = *tty_generic_fops();
  pts_fops.open = pts_open;
  pts_fops.release = pts_release;
  register_chrdev(MKDEV(5, 2), "ptmx", &ptmx_fops, NULL);
  devfs_create("ptmx", S_IFCHR | 0666, MKDEV(5, 2));
  return 0;
}
device_initcall(pty_init);
