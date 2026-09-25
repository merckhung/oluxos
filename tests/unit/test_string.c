#include <string.h>
#include "check.h"
#include "kapi.h"

int main(void) {
  unsigned char a[256], b[256], c[256];
  srand(1);
  for (int iter = 0; iter < 20000; iter++) {
    for (int i = 0; i < 256; i++) a[i] = rand();
    size_t off = rand() % 64, len = rand() % 160, doff = rand() % 64;
    memcpy(b, a, 256);
    memcpy(c, a, 256);
    k_memmove(b + doff, b + off, len);
    memmove(c + doff, c + off, len);
    CHECK(!memcmp(b, c, 256));
    k_memset(b + off, iter & 0xff, len);
    memset(c + off, iter & 0xff, len);
    CHECK(!memcmp(b, c, 256));
    k_memcpy(b + doff, a + off, len);
    memcpy(c + doff, a + off, len);
    CHECK(!memcmp(b, c, 256));
    int x = k_memcmp(a, b, len), y = memcmp(a, b, len);
    CHECK((x < 0) == (y < 0) && (x == 0) == (y == 0));
  }
  char buf[8];
  CHECK(k_strlcpy(buf, "hello world", sizeof(buf)) == 11 && !strcmp(buf, "hello w"));
  CHECK(k_strlcpy(buf, "", sizeof(buf)) == 0 && buf[0] == 0);
  strcpy(buf, "ab");
  CHECK(k_strlcat(buf, "cdefghij", sizeof(buf)) == 10 && !strcmp(buf, "abcdefg"));
  CHECK(k_strcmp("abc", "abd") < 0 && k_strcmp("b", "a") > 0 && k_strcmp("x", "x") == 0);
  CHECK(k_strncmp("abcdef", "abcxyz", 3) == 0 && k_strncmp("ab", "abc", 3) < 0);
  CHECK(k_strstr("hello world", "o w") != NULL && k_strstr("abc", "d") == NULL);
  char *end;
  CHECK(k_strtoul("0x1fZ", &end, 0) == 0x1f && *end == 'Z');
  CHECK(k_strtoul("0755", NULL, 0) == 0755);
  CHECK(k_strtol("-42", NULL, 10) == -42);
  CHECK(k_strtoul("123", NULL, 10) == 123);
  return DONE();
}
