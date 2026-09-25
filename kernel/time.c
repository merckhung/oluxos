/*
 * Timekeeping and per-CPU one-shot timers. Each CPU keeps a sorted list of
 * pending ktimers plus its periodic scheduler tick, and programs the
 * hardware compare register for whichever comes first.
 */
#include <olux/kernel.h>
#include <olux/sched.h>
#include <olux/smp.h>
#include <olux/time.h>

static const struct clock_ops *clock;
static s64 realtime_offset; /* realtime = monotonic + offset */
volatile u64 jiffies;

struct cpu_timers {
  spinlock_t lock;
  struct list_head pending;
  u64 next_tick;
};
static struct cpu_timers cpu_timers[NR_CPUS];

void register_clock(const struct clock_ops *ops) { clock = ops; }

u64 ktime_ns(void) { return clock ? clock->read_ns() : 0; }
u64 ktime_realtime_ns(void) { return ktime_ns() + realtime_offset; }
void set_realtime_ns(u64 ns) { realtime_offset = (s64)ns - (s64)ktime_ns(); }

void udelay(u64 us) {
  u64 end = ktime_ns() + us * NSEC_PER_USEC;
  while (ktime_ns() < end) __asm__ volatile("yield");
}
void mdelay(u64 ms) { udelay(ms * 1000); }

static void reprogram(struct cpu_timers *ct) {
  u64 next = ct->next_tick;
  if (!list_empty(&ct->pending)) {
    struct ktimer *t = list_first_entry(&ct->pending, struct ktimer, link);
    if (t->expires < next) next = t->expires;
  }
  clock->program(next);
}

void ktimer_init(struct ktimer *t, void (*fn)(struct ktimer *), void *arg) {
  list_init(&t->link);
  t->fn = fn;
  t->arg = arg;
  t->pending = false;
  t->cpu = -1;
}

void ktimer_start(struct ktimer *t, u64 expires) {
  ktimer_cancel(t);
  int cpu = smp_processor_id();
  struct cpu_timers *ct = &cpu_timers[cpu];
  unsigned long f = spin_lock_irqsave(&ct->lock);
  t->expires = expires;
  t->cpu = cpu;
  t->pending = true;
  struct list_head *pos;
  list_for_each(pos, &ct->pending) {
    if (list_entry(pos, struct ktimer, link)->expires > expires) break;
  }
  list_add_tail(&t->link, pos);
  if (ct->pending.next == &t->link) reprogram(ct);
  spin_unlock_irqrestore(&ct->lock, f);
}

bool ktimer_cancel(struct ktimer *t) {
  int cpu = READ_ONCE(t->cpu);
  if (cpu < 0) return false;
  struct cpu_timers *ct = &cpu_timers[cpu];
  unsigned long f = spin_lock_irqsave(&ct->lock);
  bool was = t->pending;
  if (was) {
    list_del(&t->link);
    t->pending = false;
  }
  t->cpu = -1;
  spin_unlock_irqrestore(&ct->lock, f);
  return was;
}

void timer_interrupt(void) {
  struct cpu_timers *ct = &cpu_timers[smp_processor_id()];
  u64 now = ktime_ns();
  bool tick = false;
  spin_lock(&ct->lock);
  if (now >= ct->next_tick) {
    tick = true;
    /* skip missed ticks instead of firing a burst */
    u64 missed = (now - ct->next_tick) / TICK_NSEC;
    ct->next_tick += (missed + 1) * TICK_NSEC;
  }
  while (!list_empty(&ct->pending)) {
    struct ktimer *t = list_first_entry(&ct->pending, struct ktimer, link);
    if (t->expires > now) break;
    list_del(&t->link);
    t->pending = false;
    spin_unlock(&ct->lock);
    t->fn(t);
    spin_lock(&ct->lock);
  }
  reprogram(ct);
  spin_unlock(&ct->lock);
  if (tick) {
    if (smp_processor_id() == 0) jiffies++;
    scheduler_tick();
  }
}

void time_cpu_init(void) {
  struct cpu_timers *ct = &cpu_timers[smp_processor_id()];
  spin_lock_init(&ct->lock);
  list_init(&ct->pending);
  if (!clock) panic("no clocksource registered");
  clock->cpu_init();
  unsigned long f = spin_lock_irqsave(&ct->lock);
  ct->next_tick = ktime_ns() + TICK_NSEC;
  reprogram(ct);
  spin_unlock_irqrestore(&ct->lock, f);
}
