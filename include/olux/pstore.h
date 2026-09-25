/* Persistent kernel log across warm resets (see kernel/pstore.c). */
#ifndef OLUX_PSTORE_H
#define OLUX_PSTORE_H

#include <olux/types.h>

enum pstore_state {
  PSTORE_RUNNING = 1,
  PSTORE_RESTART,  /* orderly reboot */
  PSTORE_POWEROFF, /* orderly power-off or halt */
  PSTORE_PANIC,    /* kernel panic or oops */
  PSTORE_WATCHDOG, /* watchdog expired */
};

void pstore_reserve(void); /* early, before any memblock allocation */
void pstore_init(void);    /* once ioremap works */
void pstore_write(const char *s, size_t n);
void pstore_set_state(enum pstore_state st);
/* The previous boot's log (NULL if none) and how that boot ended. */
const char *pstore_last_log(size_t *len);
const char *pstore_last_reason(void);

#endif
