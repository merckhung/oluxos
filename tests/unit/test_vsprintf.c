#include <string.h>
#include "check.h"
#include "kapi.h"

static void cmp(const char *fmt, ...) {
  char k[256], h[256];
  va_list ap, ap2;
  va_start(ap, fmt);
  va_copy(ap2, ap);
  int kn = k_vsnprintf(k, sizeof(k), fmt, ap);
  int hn = vsnprintf(h, sizeof(h), fmt, ap2);
  va_end(ap);
  va_end(ap2);
  if (kn != hn || strcmp(k, h)) {
    fprintf(stderr, "format %s: kernel \"%s\" (%d) host \"%s\" (%d)\n", fmt, k, kn, h, hn);
    failures++;
  }
}

int main(void) {
  static const char *ifmts[] = {"%d", "%5d", "%-5d|", "%05d", "%+d", "% d", "%x", "%#x", "%08X", "%o", "%#o",
                                "%u", "%.3d", "%8.3d", "%-8.3x|", "%hhd", "%hd", "%c"};
  static const char *lfmts[] = {"%ld", "%lx", "%#lx", "%lu", "%20ld", "%-20lx|", "%lld", "%llx", "%zu", "%zd"};
  long vals[] = {0, 1, -1, 7, 42, -42, 255, 256, 65535, -65536, 2147483647, -2147483647 - 1, 123456789};
  for (size_t f = 0; f < sizeof(ifmts) / sizeof(*ifmts); f++)
    for (size_t v = 0; v < sizeof(vals) / sizeof(*vals); v++) {
      if (!strcmp(ifmts[f], "%c") && (vals[v] < 32 || vals[v] > 126)) continue;
      cmp(ifmts[f], (int)vals[v]);
    }
  long long big[] = {0, -1, 9223372036854775807LL, -9223372036854775807LL - 1, 1234567890123LL};
  for (size_t f = 0; f < sizeof(lfmts) / sizeof(*lfmts); f++)
    for (size_t v = 0; v < 5; v++) cmp(lfmts[f], big[v]);
  cmp("%s|%10s|%-10s|%.3s|%%", "abc", "right", "left", "truncate");
  cmp("%s", "");
  cmp("mixed %d %s %x %c end", 12, "str", 0xbeef, 'Z');
  /* truncation */
  char small[8];
  int n = k_snprintf(small, sizeof(small), "%s", "0123456789");
  CHECK(n == 10 && !strcmp(small, "0123456"));
  CHECK(k_snprintf(NULL, 0, "%d", 12345) == 5);
  return DONE();
}
