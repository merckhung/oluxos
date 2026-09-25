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
/* Wall clock: realtime = monotonic + offset. NTP-style discipline: a
 * frequency correction and an offset being slewed at up to 500 ppm are
 * folded into the offset whenever the clock is read. */
static s64 realtime_offset;
static u64 disc_last;    /* monotonic time of the last fold */
static s64 slew_left_ns; /* remaining offset correction */
static s64 freq_ppb;     /* frequency correction, parts per billion */
static DEFINE_SPINLOCK(realtime_lock);
#define MAX_SLEW_PPM 500
volatile u64 jiffies;

struct cpu_timers {
  spinlock_t lock;
  struct list_head pending;
  u64 next_tick;
};
static struct cpu_timers cpu_timers[NR_CPUS];

void register_clock(const struct clock_ops *ops) { clock = ops; }

u64 ktime_ns(void) { return clock ? clock->read_ns() : 0; }
static s64 freq_rem, slew_rem; /* sub-nanosecond carries (units of 1e-9 ns / 1e-6 ns) */

static void fold(u64 now) { /* realtime_lock held */
  if (!disc_last) disc_last = now;
  s64 elapsed = (s64)(now - disc_last);
  disc_last = now;
  if (elapsed <= 0) return;
  if (freq_ppb) {
    if (elapsed < 1000000000000LL) { /* exact, carrying the remainder */
      freq_rem += elapsed * freq_ppb;
      realtime_offset += freq_rem / 1000000000;
      freq_rem %= 1000000000;
    } else {
      realtime_offset += elapsed / 1000 * freq_ppb / 1000000;
    }
  }
  if (slew_left_ns) {
    s64 max;
    if (elapsed < 1000000000000LL) {
      slew_rem += elapsed * MAX_SLEW_PPM;
      max = slew_rem / 1000000;
      slew_rem %= 1000000;
    } else {
      max = elapsed / 1000000 * MAX_SLEW_PPM;
    }
    s64 step = slew_left_ns > 0 ? MIN(slew_left_ns, max) : MAX(slew_left_ns, -max);
    realtime_offset += step;
    slew_left_ns -= step;
  } else {
    slew_rem = 0;
  }
}

u64 ktime_realtime_ns(void) {
  unsigned long f = spin_lock_irqsave(&realtime_lock);
  u64 now = ktime_ns();
  fold(now);
  u64 r = now + realtime_offset;
  spin_unlock_irqrestore(&realtime_lock, f);
  return r;
}

void set_realtime_ns(u64 ns) {
  unsigned long f = spin_lock_irqsave(&realtime_lock);
  u64 now = ktime_ns();
  fold(now);
  realtime_offset = (s64)ns - (s64)now;
  slew_left_ns = 0;
  spin_unlock_irqrestore(&realtime_lock, f);
}

void realtime_adjust(s64 step_ns, bool have_step, s64 slew_ns, bool have_slew, s64 new_freq_ppb, bool have_freq,
                     s64 *slew_left_out, s64 *freq_out) {
  unsigned long f = spin_lock_irqsave(&realtime_lock);
  fold(ktime_ns());
  if (have_step) realtime_offset += step_ns;
  if (have_slew) slew_left_ns = slew_ns;
  if (have_freq) freq_ppb = new_freq_ppb;
  if (slew_left_out) *slew_left_out = slew_left_ns;
  if (freq_out) *freq_out = freq_ppb;
  spin_unlock_irqrestore(&realtime_lock, f);
}

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

/* sysrq-t: each CPU's timer queue */
void show_timers(void) {
  u64 now = ktime_ns();
  for (int c = 0; c < nr_cpus_possible; c++) {
    struct cpu_timers *ct = &cpu_timers[c];
    int n = 0;
    struct list_head *pos;
    list_for_each(pos, &ct->pending) n++;
    struct ktimer *first = n ? list_first_entry(&ct->pending, struct ktimer, link) : NULL;
    pr_emerg("cpu%d timers: %d pending, next tick %+lld us, first %+lld us\n", c, n,
             (long long)(ct->next_tick - now) / 1000, first ? (long long)(first->expires - now) / 1000 : 0LL);
  }
}
