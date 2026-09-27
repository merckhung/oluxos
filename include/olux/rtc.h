/* Real-time clock core: one battery-backed clock that sets the system
 * time at boot and is updated when the system time is set. */
#ifndef OLUX_RTC_H
#define OLUX_RTC_H

#include <olux/types.h>

struct rtc_ops {
  const char *name;
  int (*read)(void *priv, u64 *secs); /* seconds since the epoch, UTC */
  int (*set)(void *priv, u64 secs);
};

int rtc_register(const struct rtc_ops *ops, void *priv);
/* Called when the system time is stepped (settimeofday, NTP). */
void rtc_set_time(u64 ns);

#endif
