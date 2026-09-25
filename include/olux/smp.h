#ifndef OLUX_SMP_H
#define OLUX_SMP_H

#include <asm/sysreg.h>
#include <olux/list.h>
#include <olux/spinlock.h>
#include <olux/types.h>

#define NR_CPUS 8

struct thread;

/* Per-CPU state, pointed to by TPIDR_EL1. */
struct cpu {
  int id;
  u64 mpidr;
  bool online;
  struct thread *curr;
  struct thread *idle;
  /* scheduler */
  spinlock_t rq_lock;
  struct list_head runqueue[32]; /* one FIFO per priority level */
  u32 rq_bitmap;                 /* bit n set: runqueue[n] non-empty */
  unsigned nr_running;
  bool need_resched;
  u64 ticks, idle_ticks, irq_count;
  u64 ctx_switches;
  /* timers */
  u64 timer_period;
  /* in-IRQ nesting */
  int irq_depth;
};

extern struct cpu cpus[NR_CPUS];
extern int nr_cpus_possible;
extern int nr_cpus_online;

static inline struct cpu *this_cpu(void) {
  return (struct cpu *)read_sysreg(tpidr_el1);
}

static inline int smp_processor_id(void) {
  struct cpu *c = this_cpu();
  return c ? c->id : 0;
}

void smp_init(void);
void smp_send_reschedule(int cpu);
void smp_send_stop(void);
void smp_call_all(void (*fn)(void *), void *arg); /* other CPUs, async */

#endif
