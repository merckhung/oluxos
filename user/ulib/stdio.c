#include "stdio.h"

#include <stdarg.h>

#include "string.h"
#include "unistd.h"

void puts(const char* s) { write(1, s, strlen(s)); }

void putc(char c) { write(1, &c, 1); }

char getch(void) {
  char c;
  read(0, &c, 1);
  return c;
}

static void print_dec(long val) {
  char buf[32];
  int i = 0;
  if (val < 0) {
    putc('-');
    val = -val;
  }
  if (val == 0) {
    putc('0');
    return;
  }
  while (val > 0) {
    buf[i++] = '0' + (val % 10);
    val /= 10;
  }
  while (i > 0) {
    putc(buf[--i]);
  }
}

static void print_hex(unsigned long val) {
  putc('0');
  putc('x');
  int i;
  int started = 0;
  for (i = 15; i >= 0; i--) {
    int digit = (val >> (i * 4)) & 0xF;
    if (digit != 0 || started || i == 0) {
      char c = digit < 10 ? '0' + digit : 'A' + digit - 10;
      putc(c);
      started = 1;
    }
  }
}

void printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  while (*fmt) {
    if (*fmt == '%') {
      fmt++;
      if (*fmt == '\0') break;

      if (*fmt == 's') {
        char* s = va_arg(args, char*);
        puts(s);
      } else if (*fmt == 'c') {
        char c = (char)va_arg(args, int);
        putc(c);
      } else if (*fmt == 'd') {
        long d = va_arg(args, long);
        print_dec(d);
      } else if (*fmt == 'x') {
        unsigned long x = va_arg(args, unsigned long);
        print_hex(x);
      } else if (*fmt == '%') {
        putc('%');
      } else {
        putc('%');
        putc(*fmt);
      }
    } else {
      putc(*fmt);
    }
    fmt++;
  }
  va_end(args);
}
