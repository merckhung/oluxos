#ifndef OLUX_REBOOT_H
#define OLUX_REBOOT_H

#include <olux/compiler.h>

/* Platform hooks, provided by PSCI or the BCM2711 watchdog driver. */
struct reboot_ops {
  const char *name;
  void (*restart)(void);
  void (*poweroff)(void);
};
void register_reboot_ops(const struct reboot_ops *ops);

void machine_restart(void) __noreturn;
void machine_halt(void) __noreturn;
void machine_poweroff(void) __noreturn;

/* Why the last reset happened (e.g. "watchdog", "power-on"), if known. */
extern const char *last_reset_reason;

#endif
