#ifndef OLUX_DEVICE_H
#define OLUX_DEVICE_H

#include <olux/compiler.h>
#include <olux/types.h>

/*
 * Device-tree driver matching. Drivers declare the compatible strings they
 * handle; start_kernel() probes every available DT node against them in
 * order of `level` (irqchip < timer < console < bus < device).
 */
enum driver_level {
  DRV_IRQCHIP = 0,
  DRV_TIMER = 1,
  DRV_CONSOLE = 2,
  DRV_FIRMWARE = 3,
  DRV_BUS = 4,
  DRV_DEVICE = 5,
  DRV_LEVELS
};

struct dt_driver {
  const char *name;
  const char *const *compatible; /* NULL-terminated */
  enum driver_level level;
  int (*probe)(int node);
};

#define DT_DRIVER(ident, lvl, probefn, ...)                                         \
  static const char *const ident##_compat[] = {__VA_ARGS__, NULL};                  \
  static const struct dt_driver ident##_drv __used __section(".drivers") = {        \
      .name = #ident, .compatible = ident##_compat, .level = lvl, .probe = probefn}

void dt_probe_level(enum driver_level level);
bool dt_node_claimed(int node);

/* Late initialisation hooks (run from the init thread, in link order). */
typedef int (*initcall_t)(void);
#define initcall(fn, lvl) \
  static const initcall_t __initcall_##fn __used __section(".initcall." #lvl) = fn
#define core_initcall(fn) initcall(fn, 1)
#define fs_initcall(fn) initcall(fn, 3)
#define device_initcall(fn) initcall(fn, 5)
#define late_initcall(fn) initcall(fn, 7)
void do_initcalls(void);

/* Helpers */
void __iomem_barrier(void);
static inline u32 readl(const volatile void *a) {
  u32 v = *(const volatile u32 *)a;
  __asm__ volatile("dmb oshld" ::: "memory");
  return v;
}
static inline void writel(u32 v, volatile void *a) {
  __asm__ volatile("dmb oshst" ::: "memory");
  *(volatile u32 *)a = v;
}
static inline u32 readl_relaxed(const volatile void *a) { return *(const volatile u32 *)a; }
static inline void writel_relaxed(u32 v, volatile void *a) { *(volatile u32 *)a = v; }
static inline u16 readw(const volatile void *a) {
  u16 v = *(const volatile u16 *)a;
  __asm__ volatile("dmb oshld" ::: "memory");
  return v;
}
static inline void writew(u16 v, volatile void *a) {
  __asm__ volatile("dmb oshst" ::: "memory");
  *(volatile u16 *)a = v;
}
static inline u8 readb(const volatile void *a) {
  u8 v = *(const volatile u8 *)a;
  __asm__ volatile("dmb oshld" ::: "memory");
  return v;
}
static inline void writeb(u8 v, volatile void *a) {
  __asm__ volatile("dmb oshst" ::: "memory");
  *(volatile u8 *)a = v;
}
static inline u64 readq(const volatile void *a) {
  u64 v = *(const volatile u64 *)a;
  __asm__ volatile("dmb oshld" ::: "memory");
  return v;
}
static inline void writeq(u64 v, volatile void *a) {
  __asm__ volatile("dmb oshst" ::: "memory");
  *(volatile u64 *)a = v;
}

/* Map the idx'th reg of a DT node. */
void *dt_ioremap(int node, int idx, u64 *size_out);

#endif
