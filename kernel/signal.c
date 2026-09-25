/*
 * POSIX signals with Linux arm64 ABI compatible signal frames.
 *
 * Pending state is protected by sig_lock (IRQ-safe) so that signals can be
 * raised from interrupt context (TTY ^C, timers). Delivery happens on the
 * way back to user space in do_signal().
 */
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/tty.h>
#include <olux/uaccess.h>
#include <olux/vm.h>

static DEFINE_SPINLOCK(sig_lock);
extern spinlock_t procs_lock;

#define SIG_ACT_TERM 0
#define SIG_ACT_CORE 1
#define SIG_ACT_IGN 2
#define SIG_ACT_STOP 3
#define SIG_ACT_CONT 4

static int default_action(int sig) {
  switch (sig) {
    case SIGCHLD:
    case SIGURG:
    case SIGWINCH:
      return SIG_ACT_IGN;
    case SIGCONT:
      return SIG_ACT_CONT;
    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
      return SIG_ACT_STOP;
    case SIGQUIT:
    case SIGILL:
    case SIGTRAP:
    case SIGABRT:
    case SIGBUS:
    case SIGFPE:
    case SIGSEGV:
    case SIGXCPU:
    case SIGXFSZ:
    case SIGSYS:
      return SIG_ACT_CORE;
    default:
      return SIG_ACT_TERM;
  }
}

static bool sig_ignored(struct process *p, int sig) {
  if (sig == SIGKILL || sig == SIGSTOP) return false;
  u64 h = p->sighand->action[sig].handler;
  return h == SIG_IGN || (h == SIG_DFL && default_action(sig) == SIG_ACT_IGN);
}

bool signal_pending(struct thread *t) {
  if (!t->proc) return false;
  sigset_t pend = t->sig_pending | t->proc->shared_pending;
  return (pend & ~t->sig_blocked) != 0 || t->proc->exiting || t->proc->group_stop;
}

bool signal_pending_current(void) { return signal_pending(current); }

void signal_wake(struct thread *t) {
  if (!try_to_wake_up(t, TASK_INTERRUPTIBLE) && t->on_cpu && t != current)
    smp_send_reschedule(t->cpu); /* make it notice on its next kernel exit */
}

static void wake_stopped(struct process *p) {
  struct thread *t;
  list_for_each_entry(t, &p->threads, thread_link) try_to_wake_up(t, TASK_STOPPED);
}

/* Called with sig_lock held. */
static void queue_signal(sigset_t *set, siginfo_t *infos, int sig, const siginfo_t *info) {
  if (!(*set & sigmask(sig))) {
    if (info) infos[sig - 1] = *info;
    else {
      memset(&infos[sig - 1], 0, sizeof(siginfo_t));
      infos[sig - 1].si_signo = sig;
      infos[sig - 1].si_code = SI_KERNEL;
    }
    infos[sig - 1].si_signo = sig;
  }
  *set |= sigmask(sig);
}

static void prepare_signal(struct process *p, int sig) {
  if (sig == SIGCONT || sig == SIGKILL) {
    sigset_t stops = sigmask(SIGSTOP) | sigmask(SIGTSTP) | sigmask(SIGTTIN) | sigmask(SIGTTOU);
    p->shared_pending &= ~stops;
    struct thread *t;
    list_for_each_entry(t, &p->threads, thread_link) t->sig_pending &= ~stops;
    if (p->group_stop) {
      p->group_stop = false;
      p->cont_reported = false;
      wake_stopped(p);
      if (p->parent) wake_up(&p->parent->child_wait);
    }
  } else if (default_action(sig) == SIG_ACT_STOP) {
    p->shared_pending &= ~sigmask(SIGCONT);
  }
}

int send_signal_thread(struct thread *t, int sig, const siginfo_t *info) {
  if (sig <= 0 || sig > NSIG) return -EINVAL;
  struct process *p = t->proc;
  if (!p) return -ESRCH;
  unsigned long f = spin_lock_irqsave(&sig_lock);
  prepare_signal(p, sig);
  if (!sig_ignored(p, sig) || (t->sig_blocked & sigmask(sig))) {
    queue_signal(&t->sig_pending, t->sig_info, sig, info);
    if (sig == SIGKILL) p->exiting = p->exiting || false;
  }
  spin_unlock_irqrestore(&sig_lock, f);
  signal_wake(t);
  return 0;
}

int send_signal_process(struct process *p, int sig, const siginfo_t *info) {
  if (sig < 0 || sig > NSIG) return -EINVAL;
  if (sig == 0) return 0;
  if (p->state == PROC_ZOMBIE) return 0;
  unsigned long f = spin_lock_irqsave(&sig_lock);
  prepare_signal(p, sig);
  bool ignored = sig_ignored(p, sig);
  if (!ignored) queue_signal(&p->shared_pending, p->shared_info, sig, info);
  spin_unlock_irqrestore(&sig_lock, f);
  if (ignored) return 0;
  /* wake one thread that does not block it (all for fatal signals) */
  struct thread *t;
  bool fatal = sig == SIGKILL;
  list_for_each_entry(t, &p->threads, thread_link) {
    if (fatal || !(t->sig_blocked & sigmask(sig))) {
      signal_wake(t);
      if (!fatal) break;
    }
  }
  return 0;
}

int kill_pgrp(int pgid, int sig, const siginfo_t *info) {
  struct process *p;
  int found = 0;
  unsigned long f = spin_lock_irqsave(&procs_lock);
  list_for_each_entry(p, &all_processes, all_link) {
    if (p->pgid == pgid && p->state != PROC_ZOMBIE) {
      send_signal_process(p, sig, info);
      found++;
    }
  }
  spin_unlock_irqrestore(&procs_lock, f);
  return found ? 0 : -ESRCH;
}

void force_sig_fault(int sig, int code, u64 addr) {
  struct thread *t = current;
  siginfo_t info = {0};
  info.si_signo = sig;
  info.si_code = code;
  info.fault.addr = addr;
  unsigned long f = spin_lock_irqsave(&sig_lock);
  /* A synchronous fault cannot be blocked or ignored: reset to default. */
  struct k_sigaction *ka = &t->proc->sighand->action[sig];
  if ((t->sig_blocked & sigmask(sig)) || ka->handler == SIG_IGN) {
    t->sig_blocked &= ~sigmask(sig);
    ka->handler = SIG_DFL;
  }
  queue_signal(&t->sig_pending, t->sig_info, sig, &info);
  spin_unlock_irqrestore(&sig_lock, f);
}

/* Dequeue the lowest-numbered deliverable signal. */
static int dequeue_signal(struct thread *t, siginfo_t *info) {
  struct process *p = t->proc;
  unsigned long f = spin_lock_irqsave(&sig_lock);
  int sig = 0;
  sigset_t pend = t->sig_pending & ~t->sig_blocked;
  if (pend) {
    sig = __builtin_ctzll(pend) + 1;
    /* prefer synchronous fault signals */
    static const int sync[] = {SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGFPE, SIGSYS};
    for (unsigned i = 0; i < ARRAY_SIZE(sync); i++)
      if (pend & sigmask(sync[i])) {
        sig = sync[i];
        break;
      }
    t->sig_pending &= ~sigmask(sig);
    *info = t->sig_info[sig - 1];
  } else {
    pend = p->shared_pending & ~t->sig_blocked;
    if (pend) {
      sig = __builtin_ctzll(pend) + 1;
      p->shared_pending &= ~sigmask(sig);
      *info = p->shared_info[sig - 1];
    }
  }
  spin_unlock_irqrestore(&sig_lock, f);
  return sig;
}

void process_stop_all(struct process *p, int sig) {
  if (p->group_stop) return;
  p->group_stop = true;
  p->stop_sig = sig;
  p->stop_reported = false;
  struct thread *t;
  list_for_each_entry(t, &p->threads, thread_link)
    if (t != current) signal_wake(t);
  if (p->parent) {
    struct k_sigaction *ka = &p->parent->sighand->action[SIGCHLD];
    if (!(ka->flags & SA_NOCLDSTOP) && ka->handler != SIG_IGN) {
      siginfo_t info = {0};
      info.si_signo = SIGCHLD;
      info.si_code = CLD_STOPPED;
      info.chld.pid = p->pid;
      info.chld.status = sig;
      send_signal_process(p->parent, SIGCHLD, &info);
    }
    wake_up(&p->parent->child_wait);
  }
}

void process_continue(struct process *p) {
  unsigned long f = spin_lock_irqsave(&sig_lock);
  prepare_signal(p, SIGCONT);
  spin_unlock_irqrestore(&sig_lock, f);
}

/* ---------------- signal frames (Linux arm64 layout) ---------------- */

#define FPSIMD_MAGIC 0x46508001
struct fpsimd_context {
  u32 magic, size;
  u32 fpsr, fpcr;
  __uint128_t vregs[32];
};
struct sigcontext {
  u64 fault_address;
  u64 regs[31];
  u64 sp;
  u64 pc;
  u64 pstate;
  u8 reserved[4096] __aligned(16);
};
struct ucontext {
  u64 uc_flags;
  u64 uc_link;
  stack_t uc_stack;
  sigset_t uc_sigmask;
  u8 unused[1024 / 8 - sizeof(sigset_t)];
  struct sigcontext uc_mcontext;
};
struct rt_sigframe {
  siginfo_t info;
  struct ucontext uc;
};
struct frame_record {
  u64 fp, lr;
};
STATIC_ASSERT(sizeof(siginfo_t) == 128, "siginfo size");
STATIC_ASSERT(offsetof(struct ucontext, uc_mcontext) == 176, "ucontext layout");

static bool on_sig_stack(struct thread *t, u64 sp) {
  return t->sigaltstack.ss_size && sp > t->sigaltstack.ss_sp && sp - t->sigaltstack.ss_sp <= t->sigaltstack.ss_size;
}

static int setup_frame(int sig, struct k_sigaction *ka, siginfo_t *info, sigset_t oldmask, struct pt_regs *regs) {
  struct thread *t = current;
  u64 sp = regs->sp;
  if ((ka->flags & SA_ONSTACK) && t->sigaltstack.ss_size && !(t->sigaltstack.ss_flags & SS_DISABLE) &&
      !on_sig_stack(t, sp))
    sp = t->sigaltstack.ss_sp + t->sigaltstack.ss_size;
  sp = ALIGN_DOWN(sp - sizeof(struct rt_sigframe) - sizeof(struct frame_record), 16);
  u64 frame_addr = sp;
  u64 rec_addr = sp + sizeof(struct rt_sigframe);

  struct rt_sigframe *f = kzalloc(sizeof(*f), 0);
  if (!f) return -ENOMEM;
  f->info = *info;
  f->uc.uc_stack = t->sigaltstack;
  f->uc.uc_stack.ss_flags = on_sig_stack(t, regs->sp) ? SS_ONSTACK : (t->sigaltstack.ss_size ? 0 : SS_DISABLE);
  f->uc.uc_sigmask = oldmask;
  struct sigcontext *sc = &f->uc.uc_mcontext;
  memcpy(sc->regs, regs->regs, sizeof(sc->regs));
  sc->sp = regs->sp;
  sc->pc = regs->pc;
  sc->pstate = regs->pstate;
  sc->fault_address = info->fault.addr;
  struct fpsimd_context *fc = (struct fpsimd_context *)sc->reserved;
  fpsimd_save(&t->fpsimd);
  fc->magic = FPSIMD_MAGIC;
  fc->size = sizeof(*fc);
  fc->fpsr = t->fpsimd.fpsr;
  fc->fpcr = t->fpsimd.fpcr;
  memcpy(fc->vregs, t->fpsimd.vregs, sizeof(fc->vregs));
  /* terminator record follows (already zero) */
  struct frame_record rec = {regs->regs[29], regs->regs[30]};
  int r = copy_to_user(frame_addr, f, sizeof(*f));
  kfree(f);
  if (r || copy_to_user(rec_addr, &rec, sizeof(rec))) return -EFAULT;

  u64 restorer = (ka->flags & SA_RESTORER) ? ka->restorer : t->proc->mm->sigtramp;
  regs->regs[0] = sig;
  regs->regs[1] = frame_addr + offsetof(struct rt_sigframe, info);
  regs->regs[2] = frame_addr + offsetof(struct rt_sigframe, uc);
  regs->regs[29] = rec_addr;
  regs->regs[30] = restorer;
  regs->sp = frame_addr;
  regs->pc = ka->handler;
  regs->pstate &= ~(1UL << 21); /* clear PSTATE.SS */
  return 0;
}

long sys_rt_sigreturn(void);
long sys_rt_sigreturn(void) {
  struct thread *t = current;
  struct pt_regs *regs = t->user_regs;
  u64 frame_addr = regs->sp;
  struct rt_sigframe *f = kmalloc(sizeof(*f), 0);
  if (!f) goto bad;
  if ((frame_addr & 15) || copy_from_user(f, frame_addr, sizeof(*f))) {
    kfree(f);
    goto bad;
  }
  struct sigcontext *sc = &f->uc.uc_mcontext;
  /* only NZCV may be changed by user space; always return to EL0t */
  u64 pstate = sc->pstate & (0xfUL << 28);
  memcpy(regs->regs, sc->regs, sizeof(regs->regs));
  regs->sp = sc->sp;
  regs->pc = sc->pc;
  regs->pstate = pstate;
  regs->syscallno = -1; /* no restart */
  struct fpsimd_context *fc = (struct fpsimd_context *)sc->reserved;
  if (fc->magic == FPSIMD_MAGIC && fc->size >= sizeof(*fc)) {
    t->fpsimd.fpsr = fc->fpsr;
    t->fpsimd.fpcr = fc->fpcr;
    memcpy(t->fpsimd.vregs, fc->vregs, sizeof(fc->vregs));
    fpsimd_load(&t->fpsimd);
  }
  unsigned long fl = spin_lock_irqsave(&sig_lock);
  t->sig_blocked = f->uc.uc_sigmask & ~SIG_KERNEL_ONLY;
  spin_unlock_irqrestore(&sig_lock, fl);
  if (!(f->uc.uc_stack.ss_flags & SS_DISABLE) && f->uc.uc_stack.ss_size >= 2048 && !on_sig_stack(t, regs->sp)) {
    t->sigaltstack.ss_sp = f->uc.uc_stack.ss_sp;
    t->sigaltstack.ss_size = f->uc.uc_stack.ss_size;
  }
  kfree(f);
  return regs->regs[0];
bad:
  force_sig_fault(SIGSEGV, SI_KERNEL, frame_addr);
  return 0;
}

/* ---------------- delivery ---------------- */

static void handle_restart(struct pt_regs *regs, struct k_sigaction *ka) {
  if (regs->syscallno < 0) return;
  long ret = (long)regs->regs[0];
  switch (ret) {
    case -ERESTARTNOHAND:
      if (ka) {
        regs->regs[0] = -EINTR;
        return;
      }
      break;
    case -ERESTARTSYS:
      if (ka && !(ka->flags & SA_RESTART)) {
        regs->regs[0] = -EINTR;
        return;
      }
      break;
    default:
      return;
  }
  regs->regs[0] = regs->orig_x0;
  regs->pc -= 4;
}

void do_signal(struct pt_regs *regs) {
  struct thread *t = current;
  struct process *p = t->proc;
  if (!p) return;
  for (;;) {
    if (p->exiting) do_exit(0);
    if (p->group_stop) {
      t->state = TASK_STOPPED;
      if (p->group_stop) schedule();
      t->state = TASK_RUNNING;
      continue;
    }
    siginfo_t info;
    int sig = dequeue_signal(t, &info);
    if (!sig) break;
    struct k_sigaction *ka = &p->sighand->action[sig];
    if (ka->handler == SIG_IGN) continue;
    if (ka->handler == SIG_DFL) {
      int act = default_action(sig);
      if (act == SIG_ACT_IGN || act == SIG_ACT_CONT) continue;
      if (act == SIG_ACT_STOP) {
        if (sig != SIGSTOP && is_orphaned_pgrp(p->pgid)) continue;
        process_stop_all(p, sig);
        continue;
      }
      if (p == init_process && sig != SIGKILL) continue; /* init is protected */
      /* terminate the whole process */
      do_group_exit(sig | (act == SIG_ACT_CORE ? 0 : 0));
    }
    /* user handler */
    handle_restart(regs, ka);
    sigset_t old = t->restore_sigmask ? t->saved_sigmask : t->sig_blocked;
    t->restore_sigmask = false;
    if (setup_frame(sig, ka, &info, old, regs)) {
      force_sig_fault(SIGSEGV, SI_KERNEL, regs->sp);
      continue;
    }
    unsigned long f = spin_lock_irqsave(&sig_lock);
    t->sig_blocked |= ka->mask & ~SIG_KERNEL_ONLY;
    if (!(ka->flags & SA_NODEFER)) t->sig_blocked |= sigmask(sig);
    if (ka->flags & SA_RESETHAND) ka->handler = SIG_DFL;
    spin_unlock_irqrestore(&sig_lock, f);
    return;
  }
  /* no handler ran: restart interrupted syscalls transparently */
  handle_restart(regs, NULL);
  if (t->restore_sigmask) {
    t->restore_sigmask = false;
    t->sig_blocked = t->saved_sigmask;
  }
}

/* ---------------- syscalls ---------------- */

long sys_rt_sigaction(u64 sig, u64 act, u64 oact, u64 size);
long sys_rt_sigaction(u64 sig, u64 act, u64 oact, u64 size) {
  if (sig < 1 || sig > NSIG || size != sizeof(sigset_t)) return -EINVAL;
  struct k_sigaction *ka = &current->proc->sighand->action[sig];
  struct k_sigaction old = *ka;
  if (act) {
    struct k_sigaction n;
    if (copy_from_user(&n, act, sizeof(n))) return -EFAULT;
    if (sig == SIGKILL || sig == SIGSTOP) return -EINVAL;
    n.mask &= ~SIG_KERNEL_ONLY;
    unsigned long f = spin_lock_irqsave(&sig_lock);
    *ka = n;
    /* setting SIG_IGN discards pending instances */
    if (sig_ignored(current->proc, sig)) {
      current->proc->shared_pending &= ~sigmask(sig);
      struct thread *t;
      list_for_each_entry(t, &current->proc->threads, thread_link) t->sig_pending &= ~sigmask(sig);
    }
    spin_unlock_irqrestore(&sig_lock, f);
  }
  if (oact && copy_to_user(oact, &old, sizeof(old))) return -EFAULT;
  return 0;
}

long sys_rt_sigprocmask(u64 how, u64 set, u64 oset, u64 size);
long sys_rt_sigprocmask(u64 how, u64 set, u64 oset, u64 size) {
  if (size != sizeof(sigset_t)) return -EINVAL;
  struct thread *t = current;
  sigset_t old = t->sig_blocked;
  if (set) {
    sigset_t s;
    if (copy_from_user(&s, set, sizeof(s))) return -EFAULT;
    s &= ~SIG_KERNEL_ONLY;
    unsigned long f = spin_lock_irqsave(&sig_lock);
    if (how == SIG_BLOCK) t->sig_blocked |= s;
    else if (how == SIG_UNBLOCK) t->sig_blocked &= ~s;
    else if (how == SIG_SETMASK) t->sig_blocked = s;
    else {
      spin_unlock_irqrestore(&sig_lock, f);
      return -EINVAL;
    }
    spin_unlock_irqrestore(&sig_lock, f);
  }
  if (oset && copy_to_user(oset, &old, sizeof(old))) return -EFAULT;
  return 0;
}

long sys_rt_sigpending(u64 set, u64 size);
long sys_rt_sigpending(u64 set, u64 size) {
  sigset_t s = (current->sig_pending | current->proc->shared_pending) & current->sig_blocked;
  return copy_to_user(set, &s, sizeof(s));
}

long sys_rt_sigsuspend(u64 set, u64 size);
long sys_rt_sigsuspend(u64 set, u64 size) {
  sigset_t s;
  if (size != sizeof(sigset_t)) return -EINVAL;
  if (copy_from_user(&s, set, sizeof(s))) return -EFAULT;
  struct thread *t = current;
  t->saved_sigmask = t->sig_blocked;
  t->restore_sigmask = true;
  t->sig_blocked = s & ~SIG_KERNEL_ONLY;
  while (!signal_pending(t)) {
    t->state = TASK_INTERRUPTIBLE;
    if (!signal_pending(t)) schedule();
    t->state = TASK_RUNNING;
  }
  return -ERESTARTNOHAND;
}

long sys_rt_sigtimedwait(u64 uset, u64 uinfo, u64 uts, u64 size);
long sys_rt_sigtimedwait(u64 uset, u64 uinfo, u64 uts, u64 size) {
  sigset_t s;
  if (size != sizeof(sigset_t)) return -EINVAL;
  if (copy_from_user(&s, uset, sizeof(s))) return -EFAULT;
  long timeout = -1;
  if (uts) {
    struct timespec64 ts;
    if (copy_from_user(&ts, uts, sizeof(ts))) return -EFAULT;
    timeout = ts.tv_sec * (long)NSEC_PER_SEC + ts.tv_nsec;
  }
  struct thread *t = current;
  s &= ~SIG_KERNEL_ONLY;
  u64 deadline = timeout >= 0 ? ktime_ns() + timeout : 0;
  for (;;) {
    sigset_t saved = t->sig_blocked;
    t->sig_blocked = ~s; /* only the waited-for signals are deliverable to us here */
    siginfo_t info;
    int sig = dequeue_signal(t, &info);
    t->sig_blocked = saved;
    if (sig) {
      if (uinfo && copy_to_user(uinfo, &info, sizeof(info))) return -EFAULT;
      return sig;
    }
    if (signal_pending(t)) return -EINTR;
    if (timeout == 0) return -EAGAIN;
    t->state = TASK_INTERRUPTIBLE;
    sigset_t pend = t->sig_pending | t->proc->shared_pending;
    if (!(pend & s)) {
      if (timeout < 0) schedule();
      else {
        u64 now = ktime_ns();
        if (now >= deadline) {
          t->state = TASK_RUNNING;
          return -EAGAIN;
        }
        schedule_timeout(deadline - now);
      }
    }
    t->state = TASK_RUNNING;
  }
}

long sys_sigaltstack(u64 uss, u64 uoss);
long sys_sigaltstack(u64 uss, u64 uoss) {
  struct thread *t = current;
  stack_t old = t->sigaltstack;
  old.ss_flags = on_sig_stack(t, t->user_regs->sp) ? SS_ONSTACK : (t->sigaltstack.ss_size ? 0 : SS_DISABLE);
  if (uss) {
    stack_t n;
    if (copy_from_user(&n, uss, sizeof(n))) return -EFAULT;
    if (on_sig_stack(t, t->user_regs->sp)) return -EPERM;
    if (n.ss_flags & SS_DISABLE) {
      memset(&t->sigaltstack, 0, sizeof(t->sigaltstack));
      t->sigaltstack.ss_flags = SS_DISABLE;
    } else {
      if (n.ss_flags & ~SS_ONSTACK) return -EINVAL;
      if (n.ss_size < 2048) return -ENOMEM;
      t->sigaltstack = n;
      t->sigaltstack.ss_flags = 0;
    }
  }
  if (uoss && copy_to_user(uoss, &old, sizeof(old))) return -EFAULT;
  return 0;
}

static int check_kill_perm(struct process *p, int sig) {
  struct cred *c = &current->proc->cred;
  if (c->euid == 0 || c->euid == p->cred.uid || c->uid == p->cred.uid) return 0;
  if (sig == SIGCONT && p->sid == current->proc->sid) return 0;
  return -EPERM;
}

long sys_kill(u64 upid, u64 sig);
long sys_kill(u64 upid, u64 sig) {
  int pid = (int)upid;
  if (sig > NSIG) return -EINVAL;
  siginfo_t info = {0};
  info.si_signo = sig;
  info.si_code = SI_USER;
  info.kill.pid = current->proc->pid;
  info.kill.uid = current->proc->cred.uid;
  if (pid > 0) {
    struct process *p = process_find(pid);
    if (!p || p->state == PROC_ZOMBIE) return -ESRCH;
    int r = check_kill_perm(p, sig);
    if (r) return r;
    return send_signal_process(p, sig, &info);
  }
  int pgid = pid == 0 ? current->proc->pgid : pid == -1 ? 0 : -pid;
  int count = 0, err = -ESRCH;
  struct process *p;
  list_for_each_entry(p, &all_processes, all_link) {
    if (p->state == PROC_ZOMBIE) continue;
    if (pid == -1) {
      if (p->pid == 1 || p == current->proc) continue;
    } else if (p->pgid != pgid) {
      continue;
    }
    if (check_kill_perm(p, sig)) {
      err = -EPERM;
      continue;
    }
    send_signal_process(p, sig, &info);
    count++;
  }
  return count ? 0 : err;
}

long sys_tgkill(u64 tgid, u64 tid, u64 sig);
long sys_tgkill(u64 tgid, u64 tid, u64 sig) {
  if (sig > NSIG) return -EINVAL;
  struct thread *t = thread_find((int)tid);
  if (!t || !t->proc || ((int)tgid > 0 && t->proc->pid != (int)tgid)) return -ESRCH;
  if (sig == 0) return 0;
  siginfo_t info = {0};
  info.si_signo = sig;
  info.si_code = SI_TKILL;
  info.kill.pid = current->proc->pid;
  return send_signal_thread(t, sig, &info);
}

long sys_tkill(u64 tid, u64 sig);
long sys_tkill(u64 tid, u64 sig) { return sys_tgkill(0, tid, sig); }
