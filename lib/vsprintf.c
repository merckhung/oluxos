/*
 * Freestanding vsnprintf supporting the subset of C99 formats used in the
 * kernel: flags "-+ #0", field width and precision (also '*'), length
 * modifiers hh h l ll z t j, and conversions d i u o x X p s c %.
 */
#include <olux/printf.h>
#include <olux/string.h>

struct out {
  char *buf;
  size_t size;
  size_t pos;
};

static void emit(struct out *o, char c) {
  if (o->pos + 1 < o->size) o->buf[o->pos] = c;
  o->pos++;
}

#define F_LEFT 1
#define F_PLUS 2
#define F_SPACE 4
#define F_ALT 8
#define F_ZERO 16
#define F_UPPER 32

static void emit_num(struct out *o, unsigned long long v, bool neg, int base, int flags,
                     int width, int prec) {
  char tmp[24];
  const char *digits = (flags & F_UPPER) ? "0123456789ABCDEF" : "0123456789abcdef";
  int n = 0;
  if (v == 0 && prec != 0) tmp[n++] = '0';
  while (v) {
    tmp[n++] = digits[v % base];
    v /= base;
  }
  char sign = 0;
  if (neg) sign = '-';
  else if (flags & F_PLUS) sign = '+';
  else if (flags & F_SPACE) sign = ' ';
  const char *prefix = "";
  if ((flags & F_ALT) && base == 16) prefix = (flags & F_UPPER) ? "0X" : "0x";
  if ((flags & F_ALT) && base == 8 && (n == 0 || tmp[n - 1] != '0')) prefix = "0";
  int plen = strlen(prefix) + (sign ? 1 : 0);
  int zeros = prec > n ? prec - n : 0;
  if ((flags & F_ZERO) && !(flags & F_LEFT) && prec < 0) {
    int w = width - plen - n;
    if (w > zeros) zeros = w;
  }
  int pad = width - plen - zeros - n;
  if (!(flags & F_LEFT))
    while (pad-- > 0) emit(o, ' ');
  if (sign) emit(o, sign);
  while (*prefix) emit(o, *prefix++);
  while (zeros-- > 0) emit(o, '0');
  while (n) emit(o, tmp[--n]);
  if (flags & F_LEFT)
    while (pad-- > 0) emit(o, ' ');
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
  struct out o = {buf, size, 0};
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      emit(&o, *fmt);
      continue;
    }
    fmt++;
    int flags = 0;
    for (;; fmt++) {
      if (*fmt == '-') flags |= F_LEFT;
      else if (*fmt == '+') flags |= F_PLUS;
      else if (*fmt == ' ') flags |= F_SPACE;
      else if (*fmt == '#') flags |= F_ALT;
      else if (*fmt == '0') flags |= F_ZERO;
      else break;
    }
    int width = 0;
    if (*fmt == '*') {
      width = va_arg(ap, int);
      if (width < 0) {
        flags |= F_LEFT;
        width = -width;
      }
      fmt++;
    } else {
      while (isdigit(*fmt)) width = width * 10 + (*fmt++ - '0');
    }
    int prec = -1;
    if (*fmt == '.') {
      fmt++;
      prec = 0;
      if (*fmt == '*') {
        prec = va_arg(ap, int);
        fmt++;
      } else {
        while (isdigit(*fmt)) prec = prec * 10 + (*fmt++ - '0');
      }
    }
    int len = 0; /* 0 int, 1 long, 2 long long, -1 short, -2 char */
    for (;; fmt++) {
      if (*fmt == 'l') len++;
      else if (*fmt == 'h') len--;
      else if (*fmt == 'z' || *fmt == 't' || *fmt == 'j') len = 1;
      else break;
    }
    unsigned long long u;
    long long s;
    switch (*fmt) {
      case 'd':
      case 'i':
        if (len >= 2) s = va_arg(ap, long long);
        else if (len == 1) s = va_arg(ap, long);
        else s = va_arg(ap, int);
        if (len == -1) s = (short)s;
        if (len <= -2) s = (signed char)s;
        emit_num(&o, s < 0 ? -(unsigned long long)s : (unsigned long long)s, s < 0, 10,
                 flags, width, prec);
        break;
      case 'u':
      case 'x':
      case 'X':
      case 'o':
        if (len >= 2) u = va_arg(ap, unsigned long long);
        else if (len == 1) u = va_arg(ap, unsigned long);
        else u = va_arg(ap, unsigned int);
        if (len == -1) u = (unsigned short)u;
        if (len <= -2) u = (unsigned char)u;
        if (*fmt == 'X') flags |= F_UPPER;
        emit_num(&o, u, false, *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16,
                 flags & ~(F_PLUS | F_SPACE), width, prec);
        break;
      case 'p':
        u = (uintptr_t)va_arg(ap, void *);
        emit_num(&o, u, false, 16, F_ALT | F_ZERO, 18, -1);
        break;
      case 's': {
        const char *str = va_arg(ap, const char *);
        if (!str) str = "(null)";
        int n = prec >= 0 ? (int)strnlen(str, prec) : (int)strlen(str);
        int pad = width - n;
        if (!(flags & F_LEFT))
          while (pad-- > 0) emit(&o, ' ');
        for (int i = 0; i < n; i++) emit(&o, str[i]);
        if (flags & F_LEFT)
          while (pad-- > 0) emit(&o, ' ');
        break;
      }
      case 'c': {
        int pad = width - 1;
        if (!(flags & F_LEFT))
          while (pad-- > 0) emit(&o, ' ');
        emit(&o, (char)va_arg(ap, int));
        if (flags & F_LEFT)
          while (pad-- > 0) emit(&o, ' ');
        break;
      }
      case '%':
        emit(&o, '%');
        break;
      case '\0':
        fmt--;
        break;
      default:
        emit(&o, '%');
        emit(&o, *fmt);
        break;
    }
  }
  if (size) o.buf[o.pos < size ? o.pos : size - 1] = '\0';
  return (int)o.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}
