#include <stdio.h>
#include <stdlib.h>
static int failures;
#define CHECK(c)                                                           \
  do {                                                                     \
    if (!(c)) {                                                            \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      failures++;                                                          \
    }                                                                      \
  } while (0)
#define DONE() (printf("%s: %s\n", __FILE__, failures ? "FAILED" : "ok"), failures ? 1 : 0)
