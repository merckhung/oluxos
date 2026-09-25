#ifndef OLUX_COMPILER_H
#define OLUX_COMPILER_H

#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __section(s) __attribute__((section(s)))
#define __noreturn __attribute__((noreturn))
#define __unused __attribute__((unused))
#define __used __attribute__((used))
#define __weak __attribute__((weak))
#define __printf(a, b) __attribute__((format(printf, a, b)))
#define __always_inline inline __attribute__((always_inline))
#define __noinline __attribute__((noinline))
#define __must_check __attribute__((warn_unused_result))
#define __init __section(".init.text")
#define __initdata __section(".init.data")

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define barrier() __asm__ volatile("" ::: "memory")
#define READ_ONCE(x) (*(const volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v) (*(volatile __typeof__(x) *)&(x) = (v))

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((__typeof__(x))(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((__typeof__(x))(a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((__typeof__(x))(a) - 1)) == 0)
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) MIN(MAX(v, lo), hi)
#define BIT(n) (1UL << (n))
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (63 - (h))))

#define container_of(ptr, type, member) \
  ((type *)((char *)(ptr) - offsetof(type, member)))

#define STATIC_ASSERT(c, msg) _Static_assert(c, msg)

#endif
