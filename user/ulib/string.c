#include "string.h"

void* memcpy(void* dest, const void* src, size_t n) {
  char* d = dest;
  const char* s = src;
  while (n--) *d++ = *s++;
  return dest;
}

void* memset(void* dest, int val, size_t n) {
  char* d = dest;
  while (n--) *d++ = val;
  return dest;
}

int strcmp(const char* s1, const char* s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char*)s1 - *(unsigned char*)s2;
}

size_t strlen(const char* s) {
  size_t len = 0;
  while (s[len]) len++;
  return len;
}

char* strstr(const char* haystack, const char* needle) {
  if (!*needle) return (char*)haystack;
  for (; *haystack; haystack++) {
    if (*haystack == *needle) {
      const char* h = haystack;
      const char* n = needle;
      while (*h && *n && *h == *n) {
        h++;
        n++;
      }
      if (!*n) return (char*)haystack;
    }
  }
  return NULL;
}
