/*
 * Processes: creation (fork/vfork/clone incl. POSIX threads), exit,
 * reparenting and wait. The main thread's TID is the process PID.
 */
#include <olux/futex.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/tty.h>
#include <olux/uaccess.h>
#include <olux/vm.h>

LIST_HEAD(all_processes);
struct process *init_process;
DEFINE_SPINLOCK(procs_lock);

#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_PIDFD 0x00001000
#define CLONE_PTRACE 0x00002000
#define CLONE_VFORK 0x00004000
#define CLONE_PARENT 0x00008000
#define CLONE_THREAD 0x00010000
#define CLONE_NEWNS 0x00020000
#define CLONE_SYSVSEM 0x00040000
#define CLONE_SETTLS 0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_DETACHED 0x00400000
#define CLONE_CHILD_SETTID 0x01000000

#define WNOHANG 1
#define WUNTRACED 2
#define WEXITED 4
#define WCONTINUED 8
#define WNOWAIT 0x01000000

struct process *process_alloc(void) {
  struct process *p = kzalloc(sizeof(*p), 0);
  if (!p) return NULL;
  list_init(&p->children);
  list_init(&p->sibling);
  list_init(&p->threads);
  atomic_set(&p->refcount, 1);
  wq_init(&p->child_wait);
  p->umask = 022;
  p->start_time = ktime_ns();
  for (int i = 0; i < RLIM_NLIMITS; i++) p->rlim[i].cur = p->rlim[i].max = RLIM_INFINITY;
  p->rlim[RLIMIT_NOFILE].cur = 1024;
  p->rlim[RLIMIT_NOFILE].max = NR_OPEN_MAX;
  p->rlim[RLIMIT_STACK].cur = USER_STACK_MAX;
  p->rlim[RLIMIT_CORE].cur = 0;
  return p;
}

void process_get(struct process *p) { atomic_inc(&p->refcount); }

void process_put(struct process *p) {
  if (!p || atomic_dec_return(&p->refcount) > 0) return;
  if (p->sighand && atomic_dec_return(&p->sighand->refcount) == 0) kfree(p->sighand);
  kfree(p);
}

struct process *process_find(int pid) {
  struct process *p, *found = NULL;
  unsigned long f = spin_lock_irqsave(&procs_lock);
  list_for_each_entry(p, &all_processes, all_link) {
    if (p->pid == pid) {
      found = p;
      break;
    }
  }
  spin_unlock_irqrestore(&procs_lock, f);
  return found;
}

static void attach_thread(struct process *p, struct thread *t) {
  t->proc = p;
  list_add_tail(&t->thread_link, &p->threads);
  p->nr_threads++;
}

/* ---------------- fork / clone ---------------- */

long do_fork(u64 flags, u64 newsp, u64 ptid, u64 tls, u64 ctid, struct pt_regs *regs) {
  struct thread *parent_t = current;
  struct process *parent = parent_t->proc;
  if ((flags & CLONE_THREAD) && !(flags & CLONE_SIGHAND)) return -EINVAL;
  if ((flags & CLONE_SIGHAND) && !(flags & CLONE_VM)) return -EINVAL;
  if (flags & (CLONE_NEWNS | CLONE_PIDFD)) return -EINVAL;

  struct thread *t = thread_alloc(parent_t->name);
  if (!t) return -EAGAIN;
  t->policy = parent_t->policy;
  t->prio = parent_t->prio;
  t->rt_priority = parent_t->rt_priority;
  t->nice = parent_t->nice;
  t->cpus_allowed = parent_t->cpus_allowed;
  t->sig_blocked = parent_t->sig_blocked;
  /* FP/SIMD and TLS registers are live in the CPU for the current thread */
  fpsimd_save(&t->fpsimd);
  t->tpidr_el0 = (flags & CLONE_SETTLS) ? tls : read_sysreg(tpidr_el0);
  t->tpidrro_el0 = read_sysreg(tpidrro_el0);

  struct process *p;
  if (flags & CLONE_THREAD) {
    p = parent;
    process_get(p);
    attach_thread(p, t);
  } else {
    p = process_alloc();
    if (!p) goto fail_thread;
    p->pid = t->tid;
    p->pgid = parent->pgid;
    p->sid = parent->sid;
    p->tty = parent->tty;
    p->cred = parent->cred;
    p->umask = parent->umask;
    memcpy(p->rlim, parent->rlim, sizeof(p->rlim));
    memcpy(p->comm, parent->comm, sizeof(p->comm));
    memcpy(p->exe, parent->exe, sizeof(p->exe));
    p->cwd = parent->cwd;
    p->root = parent->root;
    path_get(&p->cwd);
    path_get(&p->root);
    if (flags & CLONE_VM) {
      p->mm = parent->mm;
      mm_get(p->mm);
    } else {
      p->mm = mm_dup(parent->mm);
      if (!p->mm) goto fail_proc;
    }
    if (flags & CLONE_FILES) {
      p->files = parent->files;
      atomic_inc(&p->files->refcount);
    } else {
      p->files = fdtable_dup(parent->files);
      if (!p->files) goto fail_proc;
    }
    if (flags & CLONE_SIGHAND) {
      p->sighand = parent->sighand;
      atomic_inc(&p->sighand->refcount);
    } else {
      p->sighand = kmalloc(sizeof(*p->sighand), 0);
      if (!p->sighand) goto fail_proc;
      memcpy(p->sighand, parent->sighand, sizeof(*p->sighand));
      atomic_set(&p->sighand->refcount, 1);
    }
    ktimer_init(&p->alarm_timer, NULL, p);
    p->parent = (flags & CLONE_PARENT) && parent->parent ? parent->parent : parent;
    p->exit_code = flags & 0xff; /* exit signal, replaced on exit */
    process_get(p); /* one reference for the thread, one for the parent's child list */
    attach_thread(p, t);
    unsigned long f = spin_lock_irqsave(&procs_lock);
    list_add_tail(&p->sibling, &p->parent->children);
    list_add_tail(&p->all_link, &all_processes);
    spin_unlock_irqrestore(&procs_lock, f);
  }

  /* child starts with a copy of the parent's user registers */
  *t->user_regs = *regs;
  t->user_regs->regs[0] = 0;
  if (newsp) t->user_regs->sp = newsp;
  t->ctx.x19 = 0; /* user thread: ret_from_fork -> ret_to_user */

  if (flags & CLONE_PARENT_SETTID) put_user((s32)t->tid, ptid);
  if (flags & CLONE_CHILD_SETTID) {
    void *kp = mm_user_page(p->mm, ctid, true);
    if (kp && (ctid & 3) == 0 && (ctid & ~PAGE_MASK) <= PAGE_SIZE - 4) *(s32 *)kp = t->tid;
  }
  if (flags & CLONE_CHILD_CLEARTID) t->clear_child_tid = ctid;

  struct completion vfork_done;
  if (flags & CLONE_VFORK) {
    init_completion(&vfork_done);
    p->vfork_done = &vfork_done;
  }
  int tid = t->tid;
  sched_add_new(t);
  if (flags & CLONE_VFORK) {
    wait_for_completion(&vfork_done);
  }
  return tid;

fail_proc:
  if (p->mm) mm_put(p->mm);
  if (p->files) fdtable_put(p->files);
  path_put(&p->cwd);
  path_put(&p->root);
  process_put(p);
fail_thread:
  thread_put(t);
  return -ENOMEM;
}

/* ---------------- exit ---------------- */

static void reparent_children(struct process *p) {
  struct process *c, *n;
  bool zombies = false;
  list_for_each_entry_safe(c, n, &p->children, sibling) {
    list_del(&c->sibling);
    c->parent = init_process;
    list_add_tail(&c->sibling, &init_process->children);
    if (c->state == PROC_ZOMBIE) zombies = true;
  }
  if (zombies) wake_up(&init_process->child_wait);
}

static void notify_parent(struct process *p) {
  struct process *parent = p->parent;
  if (!parent) return;
  struct k_sigaction *ka = &parent->sighand->action[SIGCHLD];
  siginfo_t info = {0};
  info.si_signo = SIGCHLD;
  info.chld.pid = p->pid;
  info.chld.uid = p->cred.uid;
  int status = p->exit_code;
  if ((status & 0x7f) == 0) {
    info.si_code = CLD_EXITED;
    info.chld.status = (status >> 8) & 0xff;
  } else {
    info.si_code = (status & 0x80) ? CLD_DUMPED : CLD_KILLED;
    info.chld.status = status & 0x7f;
  }
  if (ka->handler != SIG_IGN) send_signal_process(parent, SIGCHLD, &info);
  wake_up(&parent->child_wait);
}

static void exit_process(struct process *p) {
  /* timers */
  ktimer_cancel(&p->alarm_timer);
  /* resources */
  if (p->files) {
    fdtable_put(p->files);
    p->files = NULL;
  }
  if (p->mm) {
    struct mm *mm = p->mm;
    p->mm = NULL;
    switch_mm(mm, NULL);
    mm_put(mm);
  }
  path_put(&p->cwd);
  path_put(&p->root);
  p->cwd.dentry = p->root.dentry = NULL;
  if (p->vfork_done) {
    complete(p->vfork_done);
    p->vfork_done = NULL;
  }
  /* session leader exit hangs up the controlling terminal */
  if (p->tty && p->sid == p->pid) {
    struct tty *t = p->tty;
    if (t->pgrp > 0) {
      kill_pgrp(t->pgrp, SIGHUP, NULL);
      kill_pgrp(t->pgrp, SIGCONT, NULL);
    }
    t->session = 0;
    t->pgrp = 0;
  }
  if (p == init_process) panic("init exited with status %#x", p->exit_code);
  reparent_children(p);
  p->state = PROC_ZOMBIE;
  /* auto-reap if the parent ignores SIGCHLD */
  struct k_sigaction *ka = &p->parent->sighand->action[SIGCHLD];
  bool autoreap = ka->handler == SIG_IGN || (ka->flags & SA_NOCLDWAIT);
  if (autoreap && p->parent != init_process) {
    unsigned long f = spin_lock_irqsave(&procs_lock);
    list_del(&p->sibling);
    list_del(&p->all_link);
    spin_unlock_irqrestore(&procs_lock, f);
    wake_up(&p->parent->child_wait);
    process_put(p);
    return;
  }
  notify_parent(p);
}

void do_exit(int code) {
  struct thread *t = current;
  struct process *p = t->proc;
  if (!p) panic("kernel thread %s exited", t->name);
  if (!kernel_locked()) lock_kernel();

  if (t->clear_child_tid) {
    s32 zero = 0;
    if (!put_user(zero, t->clear_child_tid)) futex_wake(t->clear_child_tid, 1);
  }
  ktimer_cancel(&t->sleep_timer);
  list_del(&t->thread_link);
  p->nr_threads--;
  p->utime += t->utime;
  p->stime += t->stime;
  if (p->nr_threads == 0) {
    if (!p->exiting) p->exit_code = (code & 0xff) << 8;
    exit_process(p);
  } else if (p->exiting) {
    /* wake the thread waiting for siblings to die in exec/group exit */
    wake_up(&p->child_wait);
  }
  t->proc = NULL;
  process_put(p);
  t->state = TASK_DEAD;
  t->bkl_depth = 1; /* released by schedule() below */
  unlock_kernel();
  local_irq_disable();
  t->bkl_depth = 0;
  schedule();
  panic("dead thread %d rescheduled", t->tid);
}

void do_group_exit(int code) {
  struct process *p = current->proc;
  if (!kernel_locked()) lock_kernel();
  if (!p->exiting) {
    p->exiting = true;
    p->exit_code = code;
    struct thread *o;
    list_for_each_entry(o, &p->threads, thread_link)
      if (o != current) send_signal_thread(o, SIGKILL, NULL);
  }
  do_exit(code);
}

/* ---------------- wait ---------------- */

static bool wait_match(struct process *c, int pid) {
  if (pid > 0) return c->pid == pid;
  if (pid == -1) return true;
  if (pid == 0) return c->pgid == current->proc->pgid;
  return c->pgid == -pid;
}

static int fill_rusage(u64 uptr, struct process *c) {
  if (!uptr) return 0;
  struct {
    s64 utime_s, utime_us, stime_s, stime_us;
    s64 rest[14];
  } ru = {0};
  u64 ut = c->utime + c->cutime, st = c->stime + c->cstime;
  ru.utime_s = ut / NSEC_PER_SEC;
  ru.utime_us = ut % NSEC_PER_SEC / 1000;
  ru.stime_s = st / NSEC_PER_SEC;
  ru.stime_us = st % NSEC_PER_SEC / 1000;
  return copy_to_user(uptr, &ru, sizeof(ru));
}

long do_wait4(int pid, u64 status_uptr, int options, u64 rusage_uptr) {
  struct process *self = current->proc;
  if (options & ~(WNOHANG | WUNTRACED | WCONTINUED | WEXITED | WNOWAIT | 0x40000000 | 0x80000000))
    return -EINVAL;
  for (;;) {
    bool any = false;
    struct process *c, *n;
    list_for_each_entry_safe(c, n, &self->children, sibling) {
      if (!wait_match(c, pid)) continue;
      any = true;
      if (c->state == PROC_ZOMBIE) {
        int st = c->exit_code;
        int cpid = c->pid;
        if (status_uptr && put_user((s32)st, status_uptr)) return -EFAULT;
        if (fill_rusage(rusage_uptr, c)) return -EFAULT;
        if (options & WNOWAIT) return cpid;
        self->cutime += c->utime + c->cutime;
        self->cstime += c->stime + c->cstime;
        unsigned long f = spin_lock_irqsave(&procs_lock);
        list_del(&c->sibling);
        list_del(&c->all_link);
        spin_unlock_irqrestore(&procs_lock, f);
        process_put(c);
        return cpid;
      }
      if ((options & WUNTRACED) && c->group_stop && !c->stop_reported) {
        c->stop_reported = true;
        if (status_uptr && put_user((s32)((c->stop_sig << 8) | 0x7f), status_uptr)) return -EFAULT;
        return c->pid;
      }
      if ((options & WCONTINUED) && !c->group_stop && !c->cont_reported) {
        c->cont_reported = true;
        if (status_uptr && put_user((s32)0xffff, status_uptr)) return -EFAULT;
        return c->pid;
      }
    }
    if (!any) return -ECHILD;
    if (options & WNOHANG) return 0;
    int r = wait_event_interruptible(self->child_wait, ({
      bool ready = false;
      struct process *q;
      list_for_each_entry(q, &self->children, sibling) {
        if (!wait_match(q, pid)) continue;
        if (q->state == PROC_ZOMBIE || ((options & WUNTRACED) && q->group_stop && !q->stop_reported) ||
            ((options & WCONTINUED) && !q->group_stop && !q->cont_reported)) {
          ready = true;
          break;
        }
      }
      ready || list_empty(&self->children);
    }));
    if (r) return r;
  }
}

bool is_orphaned_pgrp(int pgid) {
  struct process *p;
  list_for_each_entry(p, &all_processes, all_link) {
    if (p->pgid != pgid || p->state == PROC_ZOMBIE) continue;
    struct process *pp = p->parent;
    if (pp && pp->pgid != pgid && pp->sid == p->sid) return false;
  }
  return true;
}

void process_init(void) {}
