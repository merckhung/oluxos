#ifndef OLUX_PRINTF_H
#define OLUX_PRINTF_H

#include <olux/compiler.h>
#include <olux/types.h>

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);

#endif
