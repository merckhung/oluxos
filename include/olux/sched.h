#ifndef OLUX_SCHED_H
#define OLUX_SCHED_H

#include <asm/ptrace.h>
#include <olux/list.h>
#include <olux/signal.h>
#include <olux/smp.h>
#include <olux/spinlock.h>
#include <olux/time.h>
#include <olux/types.h>
#include <olux/wait.h>

/* thread->state */
#define TASK_RUNNING 0
#define TASK_INTERRUPTIBLE 1
#define TASK_UNINTERRUPTIBLE 2
#define TASK_STOPPED 4
#define TASK_DEAD 16

/* Scheduling: 32 priority levels, 0 = highest. SCHED_FIFO/RR priorities
 * 1..99 map onto levels 0..23; SCHED_OTHER threads run at levels 24..30
 * (by nice value); level 31 is SCHED_IDLE. */
#define NR_PRIO 32
#define PRIO_RT_LOWEST 23
#define PRIO_NORMAL 27
#define PRIO_IDLE 31
#define SCHED_OTHER 0
#define SCHED_FIFO 1
#define SCHED_RR 2
#define SCHED_BATCH 3
#define SCHED_IDLE 5
#define RR_TIMESLICE_TICKS 5 /* 20 ms at HZ=250 */

struct cpu_context {
  u64 x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
  u64 fp, sp, pc;
};

struct fpsimd_state {
  __uint128_t vregs[32];
  u32 fpsr, fpcr;
  u64 pad;
} __aligned(16);

struct process;
struct mm;

struct thread {
  struct cpu_context ctx; /* must be first: used by cpu_switch_to */
  struct fpsimd_state fpsimd;
  u64 tpidr_el0, tpidrro_el0;

  volatile long state;
  volatile int on_cpu;
  int on_rq;
  int cpu;
  u64 cpus_allowed;
  int policy, prio, rt_priority, nice;
  int timeslice;
  struct list_head run_link;

  int tid;
  char name[16];
  bool kthread;
  struct process *proc;
  struct list_head thread_link; /* proc->threads */
  struct list_head all_link;
  atomic_t refcount;

  void *kstack_top;
  struct pt_regs *user_regs;

  struct ktimer sleep_timer;

  /* signals */
  sigset_t sig_blocked;
  sigset_t sig_waiting; /* set being waited for in sigtimedwait() */
  sigset_t sig_pending;
  siginfo_t sig_info[NSIG];
  sigset_t saved_sigmask;
  bool restore_sigmask;
  stack_t sigaltstack;

  u64 clear_child_tid;
  u64 robust_list;
  int bkl_depth;

  /* accounting (ns) */
  u64 utime, stime, start_time, last_switch;
  u64 nvcsw, nivcsw;
  int exit_code;
};

static inline struct thread *current_thread(void) { return this_cpu()->curr; }
#define current (current_thread())

/* Big kernel lock: serialises process-context kernel code across CPUs.
 * Released automatically while a thread sleeps. */
void show_threads(void); /* sysrq-t */
void show_timers(void);
void lock_kernel(void);
void unlock_kernel(void);
bool kernel_locked(void);

void sched_init(void);
void sched_cpu_init(void);
void scheduler_tick(void);
void schedule_tail(struct thread *prev);
void wake_up_thread(struct thread *t);
bool try_to_wake_up(struct thread *t, long state_mask);
void sched_add_new(struct thread *t);
void yield(void);
void cond_resched(void);
void cpu_idle(void) __noreturn;
void set_need_resched(void);
int sched_setscheduler(struct thread *t, int policy, int prio);
int sched_getscheduler(struct thread *t);
void sched_set_nice(struct thread *t, int nice);
u64 nr_running_total(void);
void sleep_ns(u64 ns);
void msleep(u64 ms);

struct thread *thread_alloc(const char *name);
void thread_free(struct thread *t);
void thread_get(struct thread *t);
void thread_put(struct thread *t);
struct thread *kthread_create(int (*fn)(void *), void *arg, const char *name);
struct thread *thread_find(int tid);

/* fpsimd / context switch (arch) */
void fpsimd_save(struct fpsimd_state *st);
void fpsimd_load(const struct fpsimd_state *st);
struct thread *cpu_switch_to(struct thread *prev, struct thread *next);

extern struct list_head all_threads;
extern spinlock_t threads_lock;

#endif
