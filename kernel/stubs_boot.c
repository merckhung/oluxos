/* TEMPORARY: stand-ins for subsystems not yet written (boot bring-up). */
#include <asm/ptrace.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/mmu_context.h>
#include <olux/reboot.h>
#include <olux/sched.h>
#include <olux/smp.h>
#include <olux/tty.h>

void el0_sync_handler(struct pt_regs *r);
void el1_sync_handler(struct pt_regs *r);
void bad_mode(struct pt_regs *r, int reason, unsigned long esr);
void serror_handler(struct pt_regs *r, unsigned long esr);
void prepare_exit_to_user(struct pt_regs *r);
void smp_handle_sgi(int sgi);
void secondary_start_kernel(void);
u64 secondary_boot_stack;

void el0_sync_handler(struct pt_regs *r) { die("el0 sync", r, read_sysreg(esr_el1)); }
void el1_sync_handler(struct pt_regs *r) { die("el1 sync", r, read_sysreg(esr_el1)); }
void bad_mode(struct pt_regs *r, int reason, unsigned long esr) { die("bad mode", r, esr); }
void serror_handler(struct pt_regs *r, unsigned long esr) { die("SError", r, esr); }
void prepare_exit_to_user(struct pt_regs *r) {}
void smp_handle_sgi(int sgi) {}
void secondary_start_kernel(void) {}
void smp_send_reschedule(int cpu) {}
void smp_send_stop(void) {}
void switch_mm(struct mm *prev, struct mm *next) {}
void machine_restart(void) { for (;;) wfi(); }
void machine_halt(void) { for (;;) wfi(); }
struct tty *tty_register(const char *name, const struct tty_ops *ops, void *priv) { return NULL; }
void tty_receive(struct tty *t, const char *buf, size_t n) { pr_info("rx: %.*s\n", (int)n, buf); }
void tty_set_console(struct tty *t) {}
bool signal_pending_current(void) { return false; }

int kernel_init(void *arg) {
  pr_info("init thread running on CPU %d\n", smp_processor_id());
  for (int i = 0; i < 5; i++) {
    msleep(500);
    pr_info("tick %d jiffies=%llu\n", i, (unsigned long long)jiffies);
  }
  void *p = kmalloc(100, 0);
  void *q = vmalloc(20000);
  pr_info("kmalloc %p vmalloc %p free pages %llu\n", p, q, (unsigned long long)nr_free_pages());
  for (;;) msleep(1000);
}
