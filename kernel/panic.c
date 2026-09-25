/*
 * panic(), BUG(), WARN() and stack unwinding with symbolised backtraces.
 * On panic the other CPUs are stopped, the log is flushed to every console
 * and the system reboots after `panic_timeout` seconds (0 = halt), so an
 * unattended device recovers instead of hanging.
 */
#include <asm/ptrace.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/reboot.h>
#include <olux/smp.h>

volatile int in_panic;
int panic_timeout = 10;

struct ksym {
  u64 addr;
  u32 name_off;
  u32 pad;
};
extern const struct ksym ksyms_table[] __attribute__((weak));
extern const u64 ksyms_count __attribute__((weak));
extern const char ksyms_names[] __attribute__((weak));

const char *ksym_lookup(u64 addr, u64 *offset) {
  if (!&ksyms_count || ksyms_count == 0) return NULL;
  u64 lo = 0, hi = ksyms_count;
  while (hi - lo > 1) {
    u64 mid = (lo + hi) / 2;
    if (ksyms_table[mid].addr <= addr) lo = mid;
    else hi = mid;
  }
  if (ksyms_table[lo].addr > addr) return NULL;
  if (offset) *offset = addr - ksyms_table[lo].addr;
  return ksyms_names + ksyms_table[lo].name_off;
}

static bool kernel_text(u64 pc) {
  extern char _stext[], _etext[];
  return pc >= (u64)_stext && pc < (u64)_etext;
}

static void print_frame(u64 pc) {
  u64 off = 0;
  const char *name = ksym_lookup(pc, &off);
  if (name) pr_emerg("  [<%016llx>] %s+%#llx\n", (unsigned long long)pc, name, (unsigned long long)off);
  else pr_emerg("  [<%016llx>] ?\n", (unsigned long long)pc);
}

/* Walk AAPCS64 frame records: [fp] = previous fp, [fp+8] = return address. */
void backtrace_from(u64 fp, u64 pc) {
  pr_emerg("Call trace:\n");
  print_frame(pc);
  for (int depth = 0; depth < 32 && fp; depth++) {
    if ((fp & 0xf) || fp < VMALLOC_START) break;
    if (!kernel_va_to_phys(fp) || !kernel_va_to_phys(fp + 8)) break;
    u64 next = ((u64 *)fp)[0];
    u64 ret = ((u64 *)fp)[1];
    if (!ret) break;
    if (kernel_text(ret)) print_frame(ret - 4);
    if (next <= fp) break;
    fp = next;
  }
}

void dump_stack(void) {
  u64 fp;
  __asm__ volatile("mov %0, x29" : "=r"(fp));
  backtrace_from(fp, (u64)dump_stack);
}

static void dump_regs(struct pt_regs *r) {
  for (int i = 0; i < 30; i += 2)
    pr_emerg("x%-2d: %016llx  x%-2d: %016llx\n", i, (unsigned long long)r->regs[i], i + 1,
             (unsigned long long)r->regs[i + 1]);
  pr_emerg("x30: %016llx  sp : %016llx\n", (unsigned long long)r->regs[30], (unsigned long long)r->sp);
  pr_emerg("pc : %016llx  pstate: %08llx\n", (unsigned long long)r->pc, (unsigned long long)r->pstate);
}

static void panic_common(void) {
  local_irq_disable();
  if (__atomic_fetch_add(&in_panic, 1, __ATOMIC_SEQ_CST) > 0) {
    for (;;) wfi(); /* another CPU is already panicking */
  }
  smp_send_stop();
}

static void __noreturn panic_finish(void) {
  pr_emerg("---[ end Kernel panic ]---\n");
  if (panic_timeout > 0) {
    pr_emerg("Rebooting in %d seconds..\n", panic_timeout);
    u64 end = ktime_ns() + (u64)panic_timeout * 1000000000ULL;
    while (ktime_ns() < end) __asm__ volatile("yield");
    machine_restart();
  }
  machine_halt();
}

void panic(const char *fmt, ...) {
  panic_common();
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  pr_emerg("Kernel panic - not syncing: %s\n", buf);
  pr_emerg("CPU: %d  OluxOS %s (%s)\n", smp_processor_id(), OLUX_VERSION, OLUX_GITREV);
  dump_stack();
  panic_finish();
}

void die(const char *msg, struct pt_regs *regs, unsigned long esr) {
  panic_common();
  pr_emerg("Internal error: %s (ESR %#lx) [#1]\n", msg, esr);
  pr_emerg("CPU: %d  OluxOS %s (%s)\n", smp_processor_id(), OLUX_VERSION, OLUX_GITREV);
  dump_regs(regs);
  backtrace_from(regs->regs[29], regs->pc);
  panic_finish();
}

void __bug(const char *file, int line, const char *cond) {
  panic("BUG at %s:%d: %s", file, line, cond);
}

void __warn(const char *file, int line, const char *cond) {
  pr_warn("WARNING at %s:%d: %s\n", file, line, cond);
  dump_stack();
}

/* -fstack-protector support */
unsigned long __stack_chk_guard = 0x595e9fbd94fda766UL;
void __stack_chk_fail(void);
void __stack_chk_fail(void) { panic("stack protector: kernel stack corrupted"); }
