/* lwIP system glue: time, memory, randomness and assertions. */
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/random.h>
#include <olux/time.h>

#include "lwip/sys.h"

u32_t sys_now(void) { return (u32_t)(ktime_ns() / 1000000); }

void *lwip_kmalloc(size_t n) { return kmalloc(n, 0); }
void *lwip_kcalloc(size_t n, size_t m) { return kzalloc(n * m, 0); }
void lwip_kfree(void *p) { kfree(p); }
unsigned int lwip_rand(void) { return (unsigned int)get_random_u64(); }

int lwip_atoi(const char *s);
int lwip_atoi(const char *s) {
  int v = 0, neg = *s == '-';
  if (neg || *s == '+') s++;
  while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
  return neg ? -v : v;
}

void lwip_diag(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintk(LOGLEVEL_INFO, fmt, ap);
  va_end(ap);
}

void lwip_assert_fail(const char *msg, const char *file, int line) {
  panic("lwip: assertion \"%s\" failed at %s:%d", msg, file, line);
}
