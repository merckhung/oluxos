/*
 * ARMv8 generic timer: the virtual counter is the clocksource and the
 * per-CPU virtual timer (PPI 27) the clockevent. Compare values are written
 * as absolute counts (CNTV_CVAL) so periodic ticks do not accumulate drift.
 */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/time.h>

static u64 freq;
static u64 mult;  /* ns = (cnt * mult) >> 32 */
static u64 cmult; /* cnt = (ns * cmult) >> 32 */
static int timer_irq = 27;

static inline u64 counter(void) {
  isb();
  return read_sysreg(cntvct_el0);
}

static u64 arch_read_ns(void) {
  return (u64)(((unsigned __int128)counter() * mult) >> 32);
}

static u64 ns_to_cycles(u64 ns) {
  return (u64)(((unsigned __int128)ns * cmult) >> 32);
}

static void arch_program(u64 expires_ns) {
  write_sysreg(cntv_cval_el0, ns_to_cycles(expires_ns));
  write_sysreg(cntv_ctl_el0, 1); /* ENABLE, unmasked */
  isb();
}

static void arch_timer_irq(int irq, void *arg) {
  write_sysreg(cntv_ctl_el0, 3); /* mask until reprogrammed */
  timer_interrupt();
}

static void arch_cpu_init(void) {
  write_sysreg(cntv_ctl_el0, 0);
  enable_irq(timer_irq);
}

static const struct clock_ops arch_clock = {
    .read_ns = arch_read_ns,
    .program = arch_program,
    .cpu_init = arch_cpu_init,
};

static int arch_timer_probe(int node) {
  freq = read_sysreg(cntfrq_el0);
  u32 dt_freq;
  if (fdt_getprop_u32(node, "clock-frequency", &dt_freq) && dt_freq) freq = dt_freq;
  if (!freq) panic("arch_timer: CNTFRQ_EL0 is zero and no clock-frequency given");
  mult = (NSEC_PER_SEC << 32) / freq;
  cmult = (freq << 32) / NSEC_PER_SEC;
  u32 irq, flags;
  if (fdt_get_irq(node, 2, &irq, &flags) == 0) timer_irq = irq; /* virtual timer */
  register_clock(&arch_clock);
  int r = request_irq(timer_irq, arch_timer_irq, NULL, "arch_timer");
  if (r) return r;
  write_sysreg(cntv_ctl_el0, 0);
  pr_info("arch_timer: %llu.%02llu MHz, virtual timer IRQ %d\n", (unsigned long long)(freq / 1000000),
          (unsigned long long)(freq / 10000 % 100), timer_irq);
  return 0;
}

DT_DRIVER(arch_timer, DRV_TIMER, arch_timer_probe, "arm,armv8-timer", "arm,armv7-timer");
