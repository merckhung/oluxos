/* poll/ppoll/select/pselect. */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/uaccess.h>

void poll_wait(struct file *f, struct wait_queue *wq, struct poll_table *pt) {
  if (!pt || !wq) return;
  for (int i = 0; i < pt->n; i++)
    if (pt->entries[i].wq == wq) return;
  if (pt->n == POLL_MAX_ENTRIES) {
    pt->overflow = true;
    return;
  }
  struct poll_entry *e = &pt->entries[pt->n++];
  e->wq = wq;
  e->w = (struct waiter){0}; /* prepare_to_wait() must not see a stale waiter */
  prepare_to_wait(wq, &e->w, TASK_INTERRUPTIBLE);
}

static void poll_cleanup(struct poll_table *pt) {
  for (int i = 0; i < pt->n; i++) finish_wait(pt->entries[i].wq, &pt->entries[i].w);
  pt->n = 0;
}

static unsigned file_poll(struct file *f, struct poll_table *pt) {
  if (!f->f_op || !f->f_op->poll) return POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM; /* regular files */
  return f->f_op->poll(f, pt);
}

struct pollfd {
  s32 fd;
  s16 events, revents;
};

/* timeout_ns < 0: infinite */
static long do_poll(struct pollfd *fds, unsigned nfds, long timeout_ns) {
  struct poll_table *pt = kmalloc(sizeof(*pt), 0);
  if (!pt) return -ENOMEM;
  pt->n = 0;
  pt->overflow = false;
  u64 deadline = timeout_ns > 0 ? ktime_ns() + timeout_ns : 0;
  long ret;
  for (;;) {
    int ready = 0;
    /* Register on wait queues while scanning; this also sets our state to
     * TASK_INTERRUPTIBLE so a wakeup between scan and sleep is not lost. */
    for (unsigned i = 0; i < nfds; i++) {
      fds[i].revents = 0;
      if (fds[i].fd < 0) continue;
      struct file *f = fget(fds[i].fd);
      if (!f) {
        fds[i].revents = POLLNVAL;
        ready++;
        continue;
      }
      unsigned m = file_poll(f, ready ? NULL : pt);
      file_put(f);
      m &= (u16)fds[i].events | POLLERR | POLLHUP | POLLNVAL;
      fds[i].revents = m;
      if (m) ready++;
    }
    if (ready || timeout_ns == 0) {
      ret = ready;
      break;
    }
    if (signal_pending_current()) {
      ret = -ERESTARTNOHAND;
      break;
    }
    current->state = TASK_INTERRUPTIBLE;
    if (timeout_ns < 0) {
      schedule();
    } else {
      u64 now = ktime_ns();
      if (now >= deadline) {
        current->state = TASK_RUNNING;
        ret = 0;
        break;
      }
      schedule_timeout(deadline - now);
    }
    poll_cleanup(pt);
    if (pt->overflow) {
      pt->overflow = false;
    }
  }
  poll_cleanup(pt);
  current->state = TASK_RUNNING;
  kfree(pt);
  return ret;
}

long sys_ppoll(u64 ufds, u64 nfds, u64 utmo, u64 usigmask, u64 sigsetsize);
long sys_ppoll(u64 ufds, u64 nfds, u64 utmo, u64 usigmask, u64 sigsetsize) {
  if (nfds > NR_OPEN_MAX) return -EINVAL;
  long timeout = -1;
  if (utmo) {
    struct timespec64 ts;
    if (copy_from_user(&ts, utmo, sizeof(ts))) return -EFAULT;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= (s64)NSEC_PER_SEC) return -EINVAL;
    timeout = ts.tv_sec > 1000000000 ? (1L << 62) : ts.tv_sec * (long)NSEC_PER_SEC + ts.tv_nsec;
  }
  sigset_t old = current->sig_blocked;
  if (usigmask) {
    sigset_t m;
    if (sigsetsize != sizeof(sigset_t)) return -EINVAL;
    if (copy_from_user(&m, usigmask, sizeof(m))) return -EFAULT;
    current->saved_sigmask = old;
    current->restore_sigmask = true;
    current->sig_blocked = m & ~SIG_KERNEL_ONLY;
  }
  struct pollfd *fds = nfds ? kmalloc(nfds * sizeof(*fds), 0) : NULL;
  if (nfds && !fds) return -ENOMEM;
  long r;
  if (nfds && copy_from_user(fds, ufds, nfds * sizeof(*fds))) r = -EFAULT;
  else {
    r = do_poll(fds, nfds, timeout);
    if (r >= 0 && nfds) {
      for (unsigned i = 0; i < nfds; i++)
        if (put_user(fds[i].revents, ufds + i * sizeof(*fds) + 6)) {
          r = -EFAULT;
          break;
        }
    }
  }
  kfree(fds);
  if (r != -ERESTARTNOHAND && usigmask) {
    current->sig_blocked = old;
    current->restore_sigmask = false;
  }
  if (r == -ERESTARTNOHAND && !usigmask) r = -EINTR;
  if (r == -ERESTARTNOHAND) r = -EINTR; /* ppoll is never restarted */
  return r;
}

long sys_pselect6(u64 n, u64 inp, u64 outp, u64 exp, u64 utmo, u64 usig);
long sys_pselect6(u64 n, u64 inp, u64 outp, u64 exp, u64 utmo, u64 usig) {
  if ((s64)n < 0 || n > NR_OPEN_MAX) return -EINVAL;
  size_t words = DIV_ROUND_UP(n, 64);
  u64 in[NR_OPEN_MAX / 64] = {0}, out[NR_OPEN_MAX / 64] = {0}, ex[NR_OPEN_MAX / 64] = {0};
  if (inp && copy_from_user(in, inp, words * 8)) return -EFAULT;
  if (outp && copy_from_user(out, outp, words * 8)) return -EFAULT;
  if (exp && copy_from_user(ex, exp, words * 8)) return -EFAULT;
  long timeout = -1;
  if (utmo) {
    struct timespec64 ts;
    if (copy_from_user(&ts, utmo, sizeof(ts))) return -EFAULT;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= (s64)NSEC_PER_SEC) return -EINVAL;
    timeout = ts.tv_sec > 1000000000 ? (1L << 62) : ts.tv_sec * (long)NSEC_PER_SEC + ts.tv_nsec;
  }
  sigset_t old = current->sig_blocked;
  if (usig) {
    struct {
      u64 ss;
      u64 len;
    } sd;
    if (copy_from_user(&sd, usig, sizeof(sd))) return -EFAULT;
    if (sd.ss) {
      sigset_t m;
      if (copy_from_user(&m, sd.ss, sizeof(m))) return -EFAULT;
      current->sig_blocked = m & ~SIG_KERNEL_ONLY;
    }
  }
  /* build a pollfd list */
  struct pollfd *fds = kmalloc(MAX(n, 1UL) * sizeof(*fds), 0);
  if (!fds) return -ENOMEM;
  unsigned cnt = 0;
  for (u64 i = 0; i < n; i++) {
    u64 bit = 1UL << (i % 64);
    short ev = 0;
    if (in[i / 64] & bit) ev |= POLLIN | POLLRDNORM | POLLHUP | POLLERR;
    if (out[i / 64] & bit) ev |= POLLOUT | POLLWRNORM | POLLERR;
    if (ex[i / 64] & bit) ev |= POLLPRI;
    if (ev) {
      fds[cnt].fd = i;
      fds[cnt].events = ev;
      cnt++;
    }
  }
  long r = do_poll(fds, cnt, timeout);
  current->sig_blocked = old;
  if (r >= 0) {
    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));
    memset(ex, 0, sizeof(ex));
    long total = 0;
    for (unsigned k = 0; k < cnt; k++) {
      int fd = fds[k].fd;
      u64 bit = 1UL << (fd % 64);
      short rv = fds[k].revents;
      if (rv & POLLNVAL) {
        r = -EBADF;
        break;
      }
      if ((fds[k].events & POLLIN) && (rv & (POLLIN | POLLRDNORM | POLLHUP | POLLERR))) in[fd / 64] |= bit, total++;
      if ((fds[k].events & POLLOUT) && (rv & (POLLOUT | POLLWRNORM | POLLERR))) out[fd / 64] |= bit, total++;
      if ((fds[k].events & POLLPRI) && (rv & POLLPRI)) ex[fd / 64] |= bit, total++;
    }
    if (r >= 0) {
      r = total;
      if ((inp && copy_to_user(inp, in, words * 8)) || (outp && copy_to_user(outp, out, words * 8)) ||
          (exp && copy_to_user(exp, ex, words * 8)))
        r = -EFAULT;
    }
  }
  kfree(fds);
  if (r == -ERESTARTNOHAND) r = -EINTR;
  return r;
}
