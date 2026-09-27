#include <olux/compiler.h>
#include <olux/string.h>

/*
 * The kernel is built with -mstrict-align because Device memory faults on
 * unaligned accesses; the word-sized fast paths below therefore only run
 * when both pointers are 8-byte aligned.
 */
void *memcpy(void *dst, const void *src, size_t n) {
  u8 *d = dst;
  const u8 *s = src;
  if ((((uintptr_t)d | (uintptr_t)s) & 7) == 0) {
    while (n >= 32) {
      u64 a = ((const u64 *)s)[0], b = ((const u64 *)s)[1];
      u64 c = ((const u64 *)s)[2], e = ((const u64 *)s)[3];
      ((u64 *)d)[0] = a;
      ((u64 *)d)[1] = b;
      ((u64 *)d)[2] = c;
      ((u64 *)d)[3] = e;
      d += 32;
      s += 32;
      n -= 32;
    }
    while (n >= 8) {
      *(u64 *)d = *(const u64 *)s;
      d += 8;
      s += 8;
      n -= 8;
    }
  }
  while (n--) *d++ = *s++;
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  u8 *d = dst;
  const u8 *s = src;
  if (d == s || n == 0) return dst;
  if (d < s || d >= s + n) return memcpy(dst, src, n);
  d += n;
  s += n;
  while (n--) *--d = *--s;
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  u8 *d = dst;
  if (((uintptr_t)d & 7) == 0 && n >= 8) {
    u64 v = (u8)c;
    v |= v << 8;
    v |= v << 16;
    v |= v << 32;
    while (n >= 8) {
      *(u64 *)d = v;
      d += 8;
      n -= 8;
    }
  }
  while (n--) *d++ = (u8)c;
  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const u8 *x = a, *y = b;
  for (; n; n--, x++, y++)
    if (*x != *y) return *x - *y;
  return 0;
}

void *memchr(const void *s, int c, size_t n) {
  const u8 *p = s;
  for (; n; n--, p++)
    if (*p == (u8)c) return (void *)p;
  return NULL;
}

size_t strlen(const char *s) {
  const char *p = s;
  while (*p) p++;
  return p - s;
}

size_t strnlen(const char *s, size_t max) {
  size_t n = 0;
  while (n < max && s[n]) n++;
  return n;
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) a++, b++;
  return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  for (; n; n--, a++, b++) {
    if (*a != *b) return (u8)*a - (u8)*b;
    if (!*a) return 0;
  }
  return 0;
}

char *strchr(const char *s, int c) {
  for (;; s++) {
    if (*s == (char)c) return (char *)s;
    if (!*s) return NULL;
  }
}

char *strrchr(const char *s, int c) {
  const char *r = NULL;
  for (;; s++) {
    if (*s == (char)c) r = s;
    if (!*s) return (char *)r;
  }
}

size_t strlcpy(char *dst, const char *src, size_t size) {
  size_t len = strlen(src);
  if (size) {
    size_t n = len >= size ? size - 1 : len;
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return len;
}

size_t strlcat(char *dst, const char *src, size_t size) {
  size_t dl = strnlen(dst, size);
  if (dl == size) return size + strlen(src);
  return dl + strlcpy(dst + dl, src, size - dl);
}

char *strsep(char **s, const char *delim) {
  char *start = *s, *p;
  if (!start) return NULL;
  for (p = start; *p; p++) {
    if (strchr(delim, *p)) {
      *p = '\0';
      *s = p + 1;
      return start;
    }
  }
  *s = NULL;
  return start;
}

char *strstr(const char *h, const char *n) {
  size_t nl = strlen(n);
  if (!nl) return (char *)h;
  for (; *h; h++)
    if (*h == *n && !strncmp(h, n, nl)) return (char *)h;
  return NULL;
}

unsigned long strtoul(const char *s, char **end, int base) {
  unsigned long v = 0;
  while (isspace(*s)) s++;
  if (*s == '+') s++;
  if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    base = 16;
  } else if (base == 0 && s[0] == '0') {
    base = 8;
  } else if (base == 0) {
    base = 10;
  }
  for (;; s++) {
    int d;
    if (isdigit(*s)) d = *s - '0';
    else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
    else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
    else break;
    if (d >= base) break;
    v = v * base + d;
  }
  if (end) *end = (char *)s;
  return v;
}

long strtol(const char *s, char **end, int base) {
  while (isspace(*s)) s++;
  if (*s == '-') return -(long)strtoul(s + 1, end, base);
  return (long)strtoul(s, end, base);
}
