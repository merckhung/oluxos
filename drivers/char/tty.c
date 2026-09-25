/*
 * TTY core and N_TTY line discipline.
 *
 * Input arrives from drivers in interrupt context (tty_receive) and is
 * processed immediately: canonical line editing, echo and signal
 * characters. Readers sleep on read_wait. Job control follows POSIX:
 * background process groups get SIGTTIN/SIGTTOU.
 */
#include <olux/device.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/tty.h>
#include <olux/uaccess.h>

#define MAX_TTYS 16
static struct tty *ttys[MAX_TTYS];
static int nttys;
static struct tty *console_tty;

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TCSBRK 0x5409
#define TCXONC 0x540a
#define TCFLSH 0x540b
#define TIOCSCTTY 0x540e
#define TIOCGPGRP 0x540f
#define TIOCSPGRP 0x5410
#define TIOCOUTQ 0x5411
#define TIOCSTI 0x5412
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define FIONREAD 0x541b
#define TIOCNOTTY 0x5422
#define TIOCGSID 0x5429
#define TIOCGPTN 0x80045430
#define TIOCSPTLCK 0x40045431
#define TIOCGDEV 0x80045432
#define FIONBIO 0x5421

static void default_termios(struct termios *t) {
  memset(t, 0, sizeof(*t));
  t->c_iflag = ICRNL | IXON | IUTF8;
  t->c_oflag = OPOST | ONLCR;
  t->c_cflag = B115200 | CS8 | CREAD;
  t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
  t->c_cc[VINTR] = 3;
  t->c_cc[VQUIT] = 28;
  t->c_cc[VERASE] = 127;
  t->c_cc[VKILL] = 21;
  t->c_cc[VEOF] = 4;
  t->c_cc[VTIME] = 0;
  t->c_cc[VMIN] = 1;
  t->c_cc[VSTART] = 17;
  t->c_cc[VSTOP] = 19;
  t->c_cc[VSUSP] = 26;
  t->c_cc[VWERASE] = 23;
  t->c_cc[VLNEXT] = 22;
}

static unsigned ibuf_count(struct tty *t) { return t->ihead - t->itail; }

/* ---- output ---- */

static void tty_out(struct tty *t, const char *s, size_t n) { t->ops->write(t, s, n); }

static void out_char(struct tty *t, char c) {
  if ((t->termios.c_oflag & OPOST) && (t->termios.c_oflag & ONLCR) && c == '\n') {
    tty_out(t, "\r\n", 2);
    t->column = 0;
    return;
  }
  if (c == '\r')
    t->column = 0;
  else if (c == '\b') {
    if (t->column) t->column--;
  } else if (c == '\t')
    t->column = (t->column | 7) + 1;
  else if (isprint((u8)c))
    t->column++;
  tty_out(t, &c, 1);
}

static void echo_char(struct tty *t, char c) {
  if (!(t->termios.c_lflag & ECHO)) {
    if (c == '\n' && (t->termios.c_lflag & ECHONL)) out_char(t, c);
    return;
  }
  if ((t->termios.c_lflag & ECHOCTL) && (u8)c < 32 && c != '\n' && c != '\t') {
    char buf[2] = {'^', (char)(c + '@')};
    tty_out(t, buf, 2);
    t->column += 2;
    return;
  }
  out_char(t, c);
}

static void erase_one(struct tty *t) {
  if (t->ihead == t->icanon_end) return;
  char c = t->ibuf[(t->ihead - 1) % TTY_BUF];
  t->ihead--;
  if ((t->termios.c_lflag & ECHO) && (t->termios.c_lflag & ECHOE)) {
    int w = ((u8)c < 32 && (t->termios.c_lflag & ECHOCTL)) ? 2 : 1;
    for (int i = 0; i < w; i++) tty_out(t, "\b \b", 3);
  }
}

/* ---- input ---- */

static void tty_signal(struct tty *t, int sig) {
  if (!(t->termios.c_lflag & NOFLSH)) t->ihead = t->itail = t->icanon_end = 0;
  if (t->pgrp > 0) kill_pgrp(t->pgrp, sig, NULL);
  wake_up(&t->read_wait);
}

static void input_char(struct tty *t, char c) {
  struct termios *tm = &t->termios;
  if (t->lnext) {
    t->lnext = false;
    goto store;
  }
  if (tm->c_iflag & IGNCR && c == '\r') return;
  if (tm->c_iflag & ICRNL && c == '\r')
    c = '\n';
  else if (tm->c_iflag & INLCR && c == '\n')
    c = '\r';

  if (tm->c_lflag & ISIG) {
    int sig = 0;
    if (c == tm->c_cc[VINTR])
      sig = SIGINT;
    else if (c == tm->c_cc[VQUIT])
      sig = SIGQUIT;
    else if (c == tm->c_cc[VSUSP])
      sig = SIGTSTP;
    if (sig && c) {
      echo_char(t, c);
      tty_signal(t, sig);
      return;
    }
  }
  if (tm->c_lflag & ICANON) {
    if (c == tm->c_cc[VERASE] || c == '\b') {
      erase_one(t);
      return;
    }
    if (c == tm->c_cc[VKILL]) {
      while (t->ihead != t->icanon_end) erase_one(t);
      return;
    }
    if (c == tm->c_cc[VWERASE] && (tm->c_lflag & IEXTEN)) {
      while (t->ihead != t->icanon_end && t->ibuf[(t->ihead - 1) % TTY_BUF] == ' ') erase_one(t);
      while (t->ihead != t->icanon_end && t->ibuf[(t->ihead - 1) % TTY_BUF] != ' ') erase_one(t);
      return;
    }
    if (c == tm->c_cc[VLNEXT] && (tm->c_lflag & IEXTEN)) {
      t->lnext = true;
      return;
    }
  }
store:
  if (ibuf_count(t) >= TTY_BUF - 1) return; /* overflow: drop */
  t->ibuf[t->ihead % TTY_BUF] = c;
  t->ihead++;
  if (tm->c_lflag & ICANON) {
    if (c == '\n' || c == tm->c_cc[VEOF] || c == tm->c_cc[VEOL]) {
      if (c != tm->c_cc[VEOF]) echo_char(t, c);
      t->icanon_end = t->ihead;
      wake_up(&t->read_wait);
      return;
    }
    echo_char(t, c);
  } else {
    echo_char(t, c);
    t->icanon_end = t->ihead;
    wake_up(&t->read_wait);
  }
}

void tty_receive(struct tty *t, const char *buf, size_t n) {
  if (!t) return;
  unsigned long f = spin_lock_irqsave(&t->lock);
  for (size_t i = 0; i < n; i++) input_char(t, buf[i]);
  spin_unlock_irqrestore(&t->lock, f);
}

/* ---- registration ---- */

struct tty *tty_alloc(const char *name, const struct tty_ops *ops, void *priv) {
  struct tty *t = kzalloc(sizeof(*t), 0);
  if (!t) return NULL;
  strlcpy(t->name, name, sizeof(t->name));
  t->ops = ops;
  t->priv = priv;
  t->index = -1;
  default_termios(&t->termios);
  t->winsize.ws_row = 24;
  t->winsize.ws_col = 80;
  spin_lock_init(&t->lock);
  wq_init(&t->read_wait);
  wq_init(&t->write_wait);
  return t;
}

void tty_free(struct tty *t) {
  unsigned long f = spin_lock_irqsave(&procs_lock);
  struct process *p;
  list_for_each_entry(p, &all_processes, all_link) if (p->tty == t) p->tty = NULL;
  spin_unlock_irqrestore(&procs_lock, f);
  kfree(t);
}

unsigned tty_input_room(struct tty *t) { return TTY_BUF - 1 - ibuf_count(t); }

struct tty *tty_register(const char *name, const struct tty_ops *ops, void *priv) {
  if (nttys == MAX_TTYS) return NULL;
  struct tty *t = kzalloc(sizeof(*t), 0);
  if (!t) return NULL;
  strlcpy(t->name, name, sizeof(t->name));
  t->ops = ops;
  t->priv = priv;
  t->index = nttys;
  default_termios(&t->termios);
  t->winsize.ws_row = 24;
  t->winsize.ws_col = 80;
  spin_lock_init(&t->lock);
  wq_init(&t->read_wait);
  wq_init(&t->write_wait);
  ttys[nttys++] = t;
  return t;
}

struct tty *tty_console(void) { return console_tty; }
void tty_set_console(struct tty *t) { console_tty = t; }

/* ---- job control ---- */

static int job_control(struct tty *t, int sig) {
  struct process *p = current->proc;
  if (!p || p->tty != t || t->pgrp <= 0 || p->pgid == t->pgrp) return 0;
  /* background process group */
  u64 mask = current->sig_blocked;
  struct k_sigaction *ka = &p->sighand->action[sig];
  if ((mask & sigmask(sig)) || ka->handler == SIG_IGN) return sig == SIGTTIN ? -EIO : 0;
  if (is_orphaned_pgrp(p->pgid)) return -EIO;
  kill_pgrp(p->pgid, sig, NULL);
  return -ERESTARTSYS;
}

/* ---- file operations ---- */

static struct tty *file_tty(struct file *f) { return f->priv; }

static bool can_read(struct tty *t) {
  if (t->hangup) return true;
  if (t->termios.c_lflag & ICANON) return t->icanon_end != t->itail;
  unsigned vmin = t->termios.c_cc[VMIN];
  return ibuf_count(t) >= MAX(vmin, 1u) || (vmin == 0 && ibuf_count(t) > 0);
}

static ssize_t tty_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct tty *t = file_tty(f);
  int jr = job_control(t, SIGTTIN);
  if (jr) return jr;
  struct termios *tm = &t->termios;
  bool canon = tm->c_lflag & ICANON;
  if (!canon && tm->c_cc[VMIN] == 0) {
    /* VMIN == 0: return immediately or after VTIME deciseconds */
    long timeout = (long)tm->c_cc[VTIME] * 100 * (long)NSEC_PER_MSEC;
    if (timeout && ibuf_count(t) == 0) {
      long r = wait_event_interruptible_timeout(t->read_wait, ibuf_count(t) > 0 || t->hangup, timeout);
      if (r == -ERESTARTSYS) return -ERESTARTSYS;
    }
  } else if (!can_read(t)) {
    if (f->flags & O_NONBLOCK) return -EAGAIN;
    int r = wait_event_interruptible(t->read_wait, can_read(t));
    if (r) return r;
  }
  size_t done = 0;
  char tmp[128];
  unsigned long fl = spin_lock_irqsave(&t->lock);
  while (done < b->len) {
    unsigned limit = canon ? t->icanon_end : t->ihead;
    size_t n = 0;
    bool eol = false;
    while (n < sizeof(tmp) && done + n < b->len && t->itail != limit) {
      char c = t->ibuf[t->itail % TTY_BUF];
      t->itail++;
      if (canon && c == tm->c_cc[VEOF]) {
        eol = true;
        break;
      }
      tmp[n++] = c;
      if (canon && c == '\n') {
        eol = true;
        break;
      }
    }
    if (!n && !eol) break;
    spin_unlock_irqrestore(&t->lock, fl);
    if (n && iob_write(b, done, tmp, n)) return done ? (ssize_t)done : -EFAULT;
    done += n;
    fl = spin_lock_irqsave(&t->lock);
    if (eol) break;
  }
  if (t->icanon_end - t->itail > TTY_BUF) t->icanon_end = t->itail;
  spin_unlock_irqrestore(&t->lock, fl);
  return done;
}

static ssize_t tty_write(struct file *f, struct iobuf *b, loff_t *pos) {
  struct tty *t = file_tty(f);
  if (t->termios.c_lflag & TOSTOP) {
    int jr = job_control(t, SIGTTOU);
    if (jr) return jr;
  }
  char tmp[256];
  size_t done = 0;
  while (done < b->len) {
    size_t n = MIN(b->len - done, sizeof(tmp));
    if (t->ops->write_room) { /* flow control (e.g. a pty master not reading) */
      if (t->hangup) return done ? (ssize_t)done : -EIO;
      if (t->ops->write_room(t) < 2 * n) {
        if (f->flags & O_NONBLOCK) return done ? (ssize_t)done : -EAGAIN;
        int r = wait_event_interruptible(t->write_wait, t->hangup || t->ops->write_room(t) >= 2 * n);
        if (r) return done ? (ssize_t)done : r;
        continue;
      }
    }
    if (iob_read(b, done, tmp, n)) return done ? (ssize_t)done : -EFAULT;
    unsigned long fl = spin_lock_irqsave(&t->lock);
    if (t->termios.c_oflag & OPOST) {
      for (size_t i = 0; i < n; i++) out_char(t, tmp[i]);
    } else {
      tty_out(t, tmp, n);
    }
    spin_unlock_irqrestore(&t->lock, fl);
    done += n;
  }
  return done;
}

static unsigned tty_poll(struct file *f, struct poll_table *pt) {
  struct tty *t = file_tty(f);
  poll_wait(f, &t->read_wait, pt);
  unsigned m = POLLOUT | POLLWRNORM;
  if (t->ops->write_room) {
    poll_wait(f, &t->write_wait, pt);
    if (t->ops->write_room(t) < 512 && !t->hangup) m = 0;
  }
  if (can_read(t) || (!(t->termios.c_lflag & ICANON) && ibuf_count(t))) m |= POLLIN | POLLRDNORM;
  if (t->hangup) m |= POLLHUP;
  return m;
}

static long tty_ioctl(struct file *f, unsigned cmd, u64 arg) {
  struct tty *t = file_tty(f);
  struct process *p = current->proc;
  switch (cmd) {
    case TCGETS:
      return copy_to_user(arg, &t->termios, sizeof(t->termios));
    case TCSETSF:
      t->ihead = t->itail = t->icanon_end = 0;
      /* fallthrough */
    case TCSETS:
    case TCSETSW: {
      struct termios nt;
      if (copy_from_user(&nt, arg, sizeof(nt))) return -EFAULT;
      unsigned long fl = spin_lock_irqsave(&t->lock);
      bool was_canon = t->termios.c_lflag & ICANON;
      struct termios old = t->termios;
      t->termios = nt;
      if (was_canon && !(nt.c_lflag & ICANON)) t->icanon_end = t->ihead;
      spin_unlock_irqrestore(&t->lock, fl);
      wake_up(&t->read_wait);
      if (t->ops->set_termios && ((old.c_cflag ^ nt.c_cflag) & (CBAUD | CSIZE | CSTOPB | PARENB | PARODD)))
        t->ops->set_termios(t, &old);
      return 0;
    }
    case TIOCGWINSZ:
      return copy_to_user(arg, &t->winsize, sizeof(t->winsize));
    case TIOCSWINSZ: {
      struct winsize w;
      if (copy_from_user(&w, arg, sizeof(w))) return -EFAULT;
      if (memcmp(&w, &t->winsize, sizeof(w))) {
        t->winsize = w;
        if (t->pgrp > 0) kill_pgrp(t->pgrp, SIGWINCH, NULL);
      }
      return 0;
    }
    case TIOCGPGRP:
      if (p->tty != t) return -ENOTTY;
      return put_user((s32)t->pgrp, arg);
    case TIOCSPGRP: {
      s32 pg;
      if (p->tty != t || t->session != p->sid) return -ENOTTY;
      if (get_user(pg, arg)) return -EFAULT;
      if (pg <= 0) return -EINVAL;
      struct process *q;
      bool ok = false;
      list_for_each_entry(q, &all_processes, all_link) if (q->pgid == pg && q->sid == p->sid) ok = true;
      if (!ok) return -EPERM;
      t->pgrp = pg;
      return 0;
    }
    case TIOCGSID:
      if (p->tty != t) return -ENOTTY;
      return put_user((s32)t->session, arg);
    case TIOCSCTTY:
      if (p->sid != p->pid) return -EPERM;
      if (p->tty == t) return 0;
      if (t->session && t->session != p->sid && !(arg == 1 && p->cred.euid == 0)) return -EPERM;
      p->tty = t;
      t->session = p->sid;
      t->pgrp = p->pgid;
      return 0;
    case TIOCNOTTY:
      if (p->tty != t) return -ENOTTY;
      if (p->sid == p->pid) {
        if (t->pgrp > 0) {
          kill_pgrp(t->pgrp, SIGHUP, NULL);
          kill_pgrp(t->pgrp, SIGCONT, NULL);
        }
        t->session = 0;
        t->pgrp = 0;
      }
      p->tty = NULL;
      return 0;
    case FIONREAD:
      return put_user((s32)((t->termios.c_lflag & ICANON) ? t->icanon_end - t->itail : ibuf_count(t)), arg);
    case TIOCOUTQ:
      return put_user((s32)0, arg);
    case TCFLSH:
      if (arg == 0 || arg == 2) t->ihead = t->itail = t->icanon_end = 0;
      return 0;
    case TCSBRK:
    case TCXONC:
      return 0;
    case TIOCSTI: {
      char c;
      if (p->tty != t && p->cred.euid != 0) return -EPERM;
      if (get_user(c, arg)) return -EFAULT;
      tty_receive(t, &c, 1);
      return 0;
    }
    case FIONBIO: {
      s32 on;
      if (get_user(on, arg)) return -EFAULT;
      if (on)
        f->flags |= O_NONBLOCK;
      else
        f->flags &= ~O_NONBLOCK;
      return 0;
    }
    default:
      return -ENOTTY;
  }
}

static int tty_open_common(struct tty *t, struct file *f) {
  if (!t) return -ENXIO;
  f->priv = t;
  t->opens++;
  t->hangup = false;
  struct process *p = current->proc;
  /* A session leader without a controlling terminal acquires this one. */
  if (p && !(f->flags & O_NOCTTY) && p->sid == p->pid && !p->tty && !t->session) {
    p->tty = t;
    t->session = p->sid;
    t->pgrp = p->pgid;
  }
  return 0;
}

static int tty_release(struct inode *i, struct file *f) {
  struct tty *t = file_tty(f);
  if (t) t->opens--;
  return 0;
}

static int ttydev_open(struct inode *i, struct file *f) {
  int idx = MINOR(i->rdev) - 64;
  if (idx < 0 || idx >= nttys) return -ENXIO;
  return tty_open_common(ttys[idx], f);
}

static int console_open(struct inode *i, struct file *f) {
  f->flags |= O_NOCTTY * 0;
  return tty_open_common(console_tty, f);
}

static int devtty_open(struct inode *i, struct file *f) {
  struct process *p = current->proc;
  if (!p || !p->tty) return -ENXIO;
  f->priv = p->tty;
  p->tty->opens++;
  return 0;
}

static const struct file_operations ttydev_fops = {.open = ttydev_open,
                                                   .release = tty_release,
                                                   .read = tty_read,
                                                   .write = tty_write,
                                                   .poll = tty_poll,
                                                   .ioctl = tty_ioctl};
static const struct file_operations console_fops = {.open = console_open,
                                                    .release = tty_release,
                                                    .read = tty_read,
                                                    .write = tty_write,
                                                    .poll = tty_poll,
                                                    .ioctl = tty_ioctl};
static const struct file_operations devtty_fops = {.open = devtty_open,
                                                   .release = tty_release,
                                                   .read = tty_read,
                                                   .write = tty_write,
                                                   .poll = tty_poll,
                                                   .ioctl = tty_ioctl};

/* Shared by the pseudo-terminal driver. */
const struct file_operations *tty_generic_fops(void) { return &ttydev_fops; }
int tty_attach(struct tty *t, struct file *f) { return tty_open_common(t, f); }
void tty_hangup(struct tty *t) {
  t->hangup = true;
  if (t->pgrp > 0) kill_pgrp(t->pgrp, SIGHUP, NULL);
  if (t->session > 0 && t->session != t->pgrp) kill_pgrp(t->session, SIGHUP, NULL);
  wake_up(&t->read_wait);
  wake_up(&t->write_wait);
}

static int tty_devices(void) {
  for (int i = 0; i < nttys; i++) {
    dev_t d = MKDEV(204, 64 + i);
    register_chrdev(d, ttys[i]->name, &ttydev_fops, NULL);
    devfs_create(ttys[i]->name, S_IFCHR | 0620, d);
  }
  register_chrdev(MKDEV(5, 0), "tty", &devtty_fops, NULL);
  register_chrdev(MKDEV(5, 1), "console", &console_fops, NULL);
  devfs_create("tty", S_IFCHR | 0666, MKDEV(5, 0));
  devfs_create("console", S_IFCHR | 0600, MKDEV(5, 1));
  return 0;
}
device_initcall(tty_devices);
