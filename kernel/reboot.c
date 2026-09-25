/* Restart / halt / power-off with the best available platform mechanism. */
#include <olux/kernel.h>
#include <olux/pstore.h>
#include <olux/reboot.h>
#include <olux/smp.h>

static const struct reboot_ops *ops[4];
static int nops;
const char *last_reset_reason = "unknown";

void register_reboot_ops(const struct reboot_ops *o) {
  if (nops < (int)ARRAY_SIZE(ops)) ops[nops++] = o;
}

static void stop_others(void) {
  local_irq_disable();
  smp_send_stop();
}

void machine_restart(void) {
  pstore_set_state(PSTORE_RESTART);
  stop_others();
  for (int i = nops - 1; i >= 0; i--)
    if (ops[i]->restart) ops[i]->restart();
  pr_emerg("reboot: no restart mechanism available; halting\n");
  for (;;) wfi();
}

void machine_poweroff(void) {
  pstore_set_state(PSTORE_POWEROFF);
  stop_others();
  pr_notice("reboot: CPUs stopped, powering off via %s\n", nops ? ops[nops - 1]->name : "none");
  for (int i = nops - 1; i >= 0; i--)
    if (ops[i]->poweroff) ops[i]->poweroff();
  pr_emerg("reboot: power-off not supported; halting\n");
  for (;;) wfi();
}

void machine_halt(void) {
  pstore_set_state(PSTORE_POWEROFF);
  stop_others();
  for (;;) wfi();
}
