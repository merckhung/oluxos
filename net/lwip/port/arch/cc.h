/* lwIP architecture definitions for the OluxOS kernel (AArch64, little endian). */
#ifndef OLUX_LWIP_CC_H
#define OLUX_LWIP_CC_H

#include <olux/types.h> /* ssize_t */

#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H 1
#define LWIP_NO_LIMITS_H 1
#define INT_MAX __INT_MAX__
#define SSIZE_MAX __LONG_MAX__

#define X8_F "02x"
#define U16_F "hu"
#define S16_F "hd"
#define X16_F "hx"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "zu"

#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_ERR_T int

void lwip_diag(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void lwip_assert_fail(const char *msg, const char *file, int line) __attribute__((noreturn));

#define LWIP_PLATFORM_DIAG(x) \
  do {                        \
    lwip_diag x;              \
  } while (0)
#define LWIP_PLATFORM_ASSERT(x) lwip_assert_fail(x, __FILE__, __LINE__)

#define lwip_htons(x) __builtin_bswap16(x)
#define lwip_htonl(x) __builtin_bswap32(x)

#endif
