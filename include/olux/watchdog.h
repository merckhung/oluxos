/*
 * Watchdog core: /dev/watchdog (Linux-compatible API) on top of one
 * hardware watchdog, or a software fallback when there is none.
 *
 * Timeouts longer than the hardware can count are handled by the core: a
 * kernel timer pings the hardware while the userspace deadline has not
 * passed, so a hung kernel (no timer interrupts) still resets the board.
 */
#ifndef OLUX_WATCHDOG_H
#define OLUX_WATCHDOG_H

#include <olux/types.h>

struct watchdog_device {
  const char *name;
  unsigned max_hw_timeout; /* seconds the hardware can count; 0 = software only */
  int (*start)(struct watchdog_device *wd, unsigned timeout);
  int (*stop)(struct watchdog_device *wd);
  int (*ping)(struct watchdog_device *wd);
  void *priv;
};

int watchdog_register(struct watchdog_device *wd);

#endif
