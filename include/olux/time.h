#ifndef OLUX_TIME_H
#define OLUX_TIME_H

#include <olux/list.h>
#include <olux/types.h>

#define HZ 250
#define NSEC_PER_SEC 1000000000ULL
#define NSEC_PER_MSEC 1000000ULL
#define NSEC_PER_USEC 1000ULL
#define TICK_NSEC (NSEC_PER_SEC / HZ)

struct timespec64 {
  s64 tv_sec;
  s64 tv_nsec;
};

/* One-shot kernel timer; callbacks run in interrupt context. */
struct ktimer {
  struct list_head link;
  u64 expires; /* monotonic ns */
  void (*fn)(struct ktimer *t);
  void *arg;
  int cpu;
  bool pending;
};

void ktimer_init(struct ktimer *t, void (*fn)(struct ktimer *), void *arg);
void ktimer_start(struct ktimer *t, u64 expires_ns);
bool ktimer_cancel(struct ktimer *t);

/* Clocksource / clockevent provided by the architecture timer. */
struct clock_ops {
  u64 (*read_ns)(void);
  void (*program)(u64 expires_ns); /* absolute, per-CPU */
  void (*cpu_init)(void);
};
void register_clock(const struct clock_ops *ops);
void timer_interrupt(void);
void time_cpu_init(void);

u64 ktime_ns(void);
u64 ktime_realtime_ns(void);
/* Clock discipline for adjtimex(): step, slew (<= 500 ppm) and frequency. */
void realtime_adjust(s64 step_ns, bool have_step, s64 slew_ns, bool have_slew, s64 freq_ppb, bool have_freq,
                     s64 *slew_left, s64 *freq);
void set_realtime_ns(u64 ns);
extern volatile u64 jiffies;

void udelay(u64 us);
void mdelay(u64 ms);

#endif
