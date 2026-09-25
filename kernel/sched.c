/*
 * Preemptive priority scheduler.
 *
 * Each CPU has 32 FIFO run queues (one per priority level) and a bitmap of
 * non-empty levels, so picking the next thread is O(1). SCHED_FIFO threads
 * run until they block or yield; SCHED_RR and SCHED_OTHER threads are
 * time-sliced. User threads are preempted on return to user space; kernel
 * code is not preempted, but sleeps release the big kernel lock.
 *
 * Wakeups place a thread on its previous CPU if that CPU is idle, otherwise
 * on an idle CPU, otherwise on the least loaded CPU, and send a reschedule
 * IPI if the woken thread should preempt the target's current thread.
 */
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/mmu_context.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/smp.h>

LIST_HEAD(all_threads);
DEFINE_SPINLOCK(threads_lock);
static DEFINE_SPINLOCK(bkl);
static atomic_t next_tid = ATOMIC_INIT(0);

/* ---------------- big kernel lock ---------------- */

void lock_kernel(void) {
  struct thread *t = current;
  if (t->bkl_depth++ == 0) spin_lock(&bkl);
}

void unlock_kernel(void) {
  struct thread *t = current;
  BUG_ON(t->bkl_depth <= 0);
  if (--t->bkl_depth == 0) spin_unlock(&bkl);
}

bool kernel_locked(void) { return current->bkl_depth > 0; }

/* ---------------- run queues ---------------- */

static void enqueue(struct cpu *rq, struct thread *t, bool head) {
  if (head) list_add(&t->run_link, &rq->runqueue[t->prio]);
  else list_add_tail(&t->run_link, &rq->runqueue[t->prio]);
  rq->rq_bitmap |= 1U << t->prio;
  rq->nr_running++;
  t->on_rq = 1;
  t->cpu = rq->id;
}

static void dequeue(struct cpu *rq, struct thread *t) {
  list_del(&t->run_link);
  if (list_empty(&rq->runqueue[t->prio])) rq->rq_bitmap &= ~(1U << t->prio);
  rq->nr_running--;
  t->on_rq = 0;
}

static struct thread *pick_next(struct cpu *rq) {
  if (!rq->rq_bitmap) return NULL;
  int p = __builtin_ctz(rq->rq_bitmap);
  struct thread *t = list_first_entry(&rq->runqueue[p], struct thread, run_link);
  dequeue(rq, t);
  return t;
}

static int curr_prio(struct cpu *rq) {
  struct thread *c = rq->curr;
  return (!c || c == rq->idle) ? NR_PRIO : c->prio;
}

static void resched_cpu(struct cpu *rq) {
  if (rq == this_cpu()) rq->need_resched = true;
  else {
    rq->need_resched = true;
    smp_send_reschedule(rq->id);
  }
}

void set_need_resched(void) { this_cpu()->need_resched = true; }

static struct cpu *select_cpu(struct thread *t) {
  struct cpu *prev = &cpus[t->cpu];
  if (prev->online && (t->cpus_allowed & (1UL << prev->id)) && prev->curr == prev->idle &&
      prev->nr_running == 0)
    return prev;
  struct cpu *best = NULL;
  for (int i = 0; i < nr_cpus_possible; i++) {
    struct cpu *c = &cpus[i];
    if (!c->online || !(t->cpus_allowed & (1UL << i))) continue;
    if (c->curr == c->idle && c->nr_running == 0) return c;
    if (!best || c->nr_running < best->nr_running) best = c;
  }
  return best ? best : prev;
}

static struct cpu *lock_thread_rq(struct thread *t) {
  for (;;) {
    struct cpu *rq = &cpus[READ_ONCE(t->cpu)];
    spin_lock(&rq->rq_lock);
    if (rq->id == READ_ONCE(t->cpu)) return rq;
    spin_unlock(&rq->rq_lock);
  }
}

static void activate(struct thread *t) {
  /* caller holds no rq lock; t->state == TASK_RUNNING, not on any rq/cpu */
  struct cpu *target = select_cpu(t);
  spin_lock(&target->rq_lock);
  enqueue(target, t, false);
  if (t->prio < curr_prio(target)) resched_cpu(target);
  spin_unlock(&target->rq_lock);
}

bool try_to_wake_up(struct thread *t, long state_mask) {
  unsigned long f = local_irq_save();
  struct cpu *rq = lock_thread_rq(t);
  if (!(t->state & state_mask)) {
    spin_unlock(&rq->rq_lock);
    local_irq_restore(f);
    return false;
  }
  t->state = TASK_RUNNING;
  if (t->on_rq || t->on_cpu) {
    /* still running (about to sleep) or already queued */
    spin_unlock(&rq->rq_lock);
    local_irq_restore(f);
    return true;
  }
  spin_unlock(&rq->rq_lock);
  activate(t);
  local_irq_restore(f);
  return true;
}

void wake_up_thread(struct thread *t) {
  try_to_wake_up(t, TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE | TASK_STOPPED);
}

void sched_add_new(struct thread *t) {
  unsigned long f = local_irq_save();
  t->state = TASK_RUNNING;
  activate(t);
  local_irq_restore(f);
}

/* ---------------- context switch ---------------- */

static void finish_switch(struct thread *prev) {
  struct cpu *rq = this_cpu();
  smp_wmb();
  prev->on_cpu = 0;
  spin_unlock(&rq->rq_lock);
  if (prev->state == TASK_DEAD) thread_put(prev); /* drop the run reference */
}

void schedule_tail(struct thread *prev) {
  finish_switch(prev);
  local_irq_enable();
}

void schedule(void) {
  struct thread *prev = current;
  int bkl_depth = prev->bkl_depth;
  if (bkl_depth) spin_unlock(&bkl);

  unsigned long f = local_irq_save();
  struct cpu *rq = this_cpu();
  spin_lock(&rq->rq_lock);
  rq->need_resched = false;

  if (prev->state == TASK_RUNNING && prev != rq->idle) {
    /* Preempted: FIFO threads keep their place, others go to the back. */
    bool head = prev->policy == SCHED_FIFO;
    enqueue(rq, prev, head);
    if (prev->timeslice <= 0) prev->timeslice = RR_TIMESLICE_TICKS;
  }
  struct thread *next = pick_next(rq);
  if (!next) next = rq->idle;

  if (next != prev) {
    u64 now = ktime_ns();
    prev->last_switch = now;
    next->last_switch = now;
    if (prev->state == TASK_RUNNING) prev->nivcsw++;
    else prev->nvcsw++;
    rq->ctx_switches++;
    next->on_cpu = 1;
    next->cpu = rq->id;
    rq->curr = next;
    if (!prev->kthread) {
      fpsimd_save(&prev->fpsimd);
      prev->tpidr_el0 = read_sysreg(tpidr_el0);
      prev->tpidrro_el0 = read_sysreg(tpidrro_el0);
    }
    if (!next->kthread) {
      fpsimd_load(&next->fpsimd);
      write_sysreg(tpidr_el0, next->tpidr_el0);
      write_sysreg(tpidrro_el0, next->tpidrro_el0);
    }
    switch_mm(prev->proc ? prev->proc->mm : NULL, next->proc ? next->proc->mm : NULL);
    prev = cpu_switch_to(prev, next);
    finish_switch(prev);
  } else {
    spin_unlock(&rq->rq_lock);
  }
  local_irq_restore(f);
  if (bkl_depth) spin_lock(&bkl);
}

void yield(void) {
  struct thread *t = current;
  t->timeslice = 0;
  schedule();
}

void cond_resched(void) {
  if (this_cpu()->need_resched) schedule();
}

/* Called from the timer interrupt on every CPU. */
u64 load_avg[3];

/* Exponentially-damped load average, updated every 5 seconds (<<16 fixed point). */
static void calc_load(void) {
  static const u64 exp[3] = {60410, 64453, 65173}; /* e^(-5/60), e^(-5/300), e^(-5/900) */
  u64 n = nr_running_total() << 16;
  for (int i = 0; i < 3; i++) load_avg[i] = (load_avg[i] * exp[i] + n * (65536 - exp[i])) >> 16;
}

void scheduler_tick(void) {
  struct cpu *rq = this_cpu();
  if (rq->id == 0 && rq->ticks % (5 * HZ) == 0) calc_load();
  struct thread *t = rq->curr;
  rq->ticks++;
  if (t == rq->idle) {
    rq->idle_ticks++;
    if (rq->nr_running) rq->need_resched = true;
    return;
  }
  if (rq->irq_regs && user_mode(rq->irq_regs)) t->utime += TICK_NSEC;
  else t->stime += TICK_NSEC;
  if (t->policy != SCHED_FIFO && --t->timeslice <= 0) {
    t->timeslice = 0;
    spin_lock(&rq->rq_lock);
    /* only switch if someone of equal or higher priority is waiting */
    if (rq->rq_bitmap && __builtin_ctz(rq->rq_bitmap) <= t->prio) rq->need_resched = true;
    else t->timeslice = RR_TIMESLICE_TICKS;
    spin_unlock(&rq->rq_lock);
  }
}

/* ---------------- sleeping ---------------- */

static void sleep_timer_fn(struct ktimer *kt) {
  try_to_wake_up(kt->arg, TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE);
}

long schedule_timeout(long ns) {
  struct thread *t = current;
  u64 end = ktime_ns() + ns;
  ktimer_init(&t->sleep_timer, sleep_timer_fn, t);
  ktimer_start(&t->sleep_timer, end);
  schedule();
  ktimer_cancel(&t->sleep_timer);
  u64 now = ktime_ns();
  return now >= end ? 0 : (long)(end - now);
}

void sleep_ns(u64 ns) {
  current->state = TASK_UNINTERRUPTIBLE;
  schedule_timeout(ns);
}

void msleep(u64 ms) { sleep_ns(ms * NSEC_PER_MSEC); }

/* ---------------- policy ---------------- */

static int compute_prio(int policy, int rt_prio, int nice) {
  if (policy == SCHED_FIFO || policy == SCHED_RR) return PRIO_RT_LOWEST - (rt_prio - 1) * PRIO_RT_LOWEST / 98;
  if (policy == SCHED_IDLE) return PRIO_IDLE;
  int p = PRIO_NORMAL + nice / 7; /* nice -20..19 -> 24..29 */
  return CLAMP(p, PRIO_RT_LOWEST + 1, PRIO_IDLE - 1);
}

int sched_setscheduler(struct thread *t, int policy, int prio) {
  if (policy == SCHED_FIFO || policy == SCHED_RR) {
    if (prio < 1 || prio > 99) return -EINVAL;
  } else if (policy == SCHED_OTHER || policy == SCHED_BATCH || policy == SCHED_IDLE) {
    if (prio != 0) return -EINVAL;
  } else {
    return -EINVAL;
  }
  unsigned long f = local_irq_save();
  struct cpu *rq = lock_thread_rq(t);
  bool queued = t->on_rq;
  if (queued) dequeue(rq, t);
  t->policy = policy;
  t->rt_priority = prio;
  t->prio = compute_prio(policy, prio, t->nice);
  if (queued) enqueue(rq, t, false);
  if (queued && t->prio < curr_prio(rq)) resched_cpu(rq);
  spin_unlock(&rq->rq_lock);
  local_irq_restore(f);
  return 0;
}

int sched_getscheduler(struct thread *t) { return t->policy; }

void sched_set_nice(struct thread *t, int nice) {
  t->nice = CLAMP(nice, -20, 19);
  if (t->policy == SCHED_OTHER || t->policy == SCHED_BATCH) sched_setscheduler(t, t->policy, 0);
}

u64 nr_running_total(void) {
  u64 n = 0;
  for (int i = 0; i < nr_cpus_possible; i++)
    n += cpus[i].nr_running + (cpus[i].curr != cpus[i].idle && cpus[i].online);
  return n;
}

/* ---------------- threads ---------------- */

extern void ret_from_fork(void);

struct thread *thread_alloc(const char *name) {
  struct thread *t = kzalloc(sizeof(*t), 0);
  if (!t) return NULL;
  t->kstack_top = vmap_stack(THREAD_STACK_SIZE);
  if (!t->kstack_top) {
    kfree(t);
    return NULL;
  }
  t->tid = (int)atomic_inc_return(&next_tid);
  strlcpy(t->name, name, sizeof(t->name));
  t->state = TASK_UNINTERRUPTIBLE;
  t->policy = SCHED_OTHER;
  t->prio = PRIO_NORMAL;
  t->timeslice = RR_TIMESLICE_TICKS;
  t->cpus_allowed = ~0UL;
  t->cpu = smp_processor_id();
  t->fpsimd.fpcr = 0;
  atomic_set(&t->refcount, 1);
  list_init(&t->run_link);
  list_init(&t->thread_link);
  t->user_regs = (struct pt_regs *)((u8 *)t->kstack_top - sizeof(struct pt_regs));
  t->ctx.sp = (u64)t->user_regs;
  t->ctx.pc = (u64)ret_from_fork;
  t->start_time = ktime_ns();
  unsigned long f = spin_lock_irqsave(&threads_lock);
  list_add_tail(&t->all_link, &all_threads);
  spin_unlock_irqrestore(&threads_lock, f);
  return t;
}

void thread_get(struct thread *t) { atomic_inc(&t->refcount); }

void thread_free(struct thread *t) {
  unsigned long f = spin_lock_irqsave(&threads_lock);
  list_del(&t->all_link);
  spin_unlock_irqrestore(&threads_lock, f);
  vfree_stack(t->kstack_top, THREAD_STACK_SIZE);
  kfree(t);
}

void thread_put(struct thread *t) {
  if (atomic_dec_return(&t->refcount) == 0) thread_free(t);
}

struct thread *thread_find(int tid) {
  struct thread *t, *found = NULL;
  unsigned long f = spin_lock_irqsave(&threads_lock);
  list_for_each_entry(t, &all_threads, all_link) {
    if (t->tid == tid && t->state != TASK_DEAD) {
      found = t;
      break;
    }
  }
  spin_unlock_irqrestore(&threads_lock, f);
  return found;
}

struct thread *kthread_create(int (*fn)(void *), void *arg, const char *name) {
  struct thread *t = thread_alloc(name);
  if (!t) return NULL;
  t->kthread = true;
  t->proc = NULL;
  t->ctx.x19 = (u64)fn;
  t->ctx.x20 = (u64)arg;
  return t;
}

/* ---------------- init ---------------- */

static void rq_init(struct cpu *c) {
  spin_lock_init(&c->rq_lock);
  for (int i = 0; i < NR_PRIO; i++) list_init(&c->runqueue[i]);
  c->rq_bitmap = 0;
  c->nr_running = 0;
}

/* Turn the boot context of this CPU into its idle thread. */
void sched_cpu_init(void) {
  struct cpu *c = this_cpu();
  rq_init(c);
  struct thread *idle = kzalloc(sizeof(*idle), 0);
  if (!idle) panic("sched: no memory for idle thread");
  snprintf(idle->name, sizeof(idle->name), "idle/%d", c->id);
  idle->tid = 0;
  idle->kthread = true;
  idle->state = TASK_RUNNING;
  idle->on_cpu = 1;
  idle->cpu = c->id;
  idle->prio = NR_PRIO;
  idle->cpus_allowed = 1UL << c->id;
  atomic_set(&idle->refcount, 1);
  list_init(&idle->run_link);
  c->idle = idle;
  c->curr = idle;
}

void sched_init(void) { sched_cpu_init(); }

void cpu_idle(void) {
  struct cpu *c = this_cpu();
  for (;;) {
    local_irq_disable();
    if (!c->need_resched && c->nr_running == 0) {
      dsb(sy);
      wfi();
    }
    local_irq_enable();
    schedule();
  }
}
