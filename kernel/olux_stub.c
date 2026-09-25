/* TEMPORARY: OluxOS IPC syscalls until the channel subsystem lands. */
#include <olux/kernel.h>

#define STUB(name) \
  long sys_##name(void); \
  long sys_##name(void) { return -ENOSYS; }
STUB(olux_channel)
STUB(olux_msg_send)
STUB(olux_msg_recv)
STUB(olux_sysinfo)
STUB(olux_watchdog)

void rtc_set_time(u64 ns);
__weak void rtc_set_time(u64 ns) {}
