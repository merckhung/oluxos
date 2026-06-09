#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#define ANY_THREAD 0xFFFFFFFF
#define UART_DRIVER_TID 1
#define RAMDISK_DRIVER_TID 2
#define FS_SERVER_TID 3
#define SHELL_TID 4

#if defined(__riscv)

static inline int sys_gettid(void) {
  register long a0 __asm__("a0");
  register long a7 __asm__("a7") = 4;
  __asm__ volatile("ecall" : "=r"(a0) : "r"(a7) : "memory");
  return a0;
}

static inline int sys_send(int dest, const void* buf, int size) {
  register long a0 __asm__("a0") = dest;
  register long a1 __asm__("a1") = (long)buf;
  register long a2 __asm__("a2") = size;
  register long a7 __asm__("a7") = 2;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
  return a0;
}

static inline int sys_recv(int src, void* buf, int size) {
  register long a0 __asm__("a0") = src;
  register long a1 __asm__("a1") = (long)buf;
  register long a2 __asm__("a2") = size;
  register long a7 __asm__("a7") = 3;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
  return a0;
}

static inline void* sys_map_mmio(unsigned long phys_addr) {
  register long a0 __asm__("a0") = phys_addr;
  register long a7 __asm__("a7") = 5;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
  return (void*)a0;
}

static inline void* sys_map_fb(void) {
  register long a0 __asm__("a0");
  register long a7 __asm__("a7") = 6;
  __asm__ volatile("ecall" : "=r"(a0) : "r"(a7) : "memory");
  return (void*)a0;
}

static inline int sys_spawn(const void* buf, int size) {
  register long a0 __asm__("a0") = (long)buf;
  register long a1 __asm__("a1") = size;
  register long a7 __asm__("a7") = 7;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
  return a0;
}

#else

static inline int sys_gettid(void) {
  register long x0 __asm__("x0");
  register long x8 __asm__("x8") = 4;
  __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
  return x0;
}

static inline int sys_send(int dest, const void* buf, int size) {
  register long x0 __asm__("x0") = dest;
  register long x1 __asm__("x1") = (long)buf;
  register long x2 __asm__("x2") = size;
  register long x8 __asm__("x8") = 2;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
  return x0;
}

static inline int sys_recv(int src, void* buf, int size) {
  register long x0 __asm__("x0") = src;
  register long x1 __asm__("x1") = (long)buf;
  register long x2 __asm__("x2") = size;
  register long x8 __asm__("x8") = 3;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
  return x0;
}

static inline void* sys_map_mmio(unsigned long phys_addr) {
  register long x0 __asm__("x0") = phys_addr;
  register long x8 __asm__("x8") = 5;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
  return (void*)x0;
}

static inline void* sys_map_fb(void) {
  register long x0 __asm__("x0");
  register long x8 __asm__("x8") = 6;
  __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
  return (void*)x0;
}

static inline int sys_spawn(const void* buf, int size) {
  register long x0 __asm__("x0") = (long)buf;
  register long x1 __asm__("x1") = size;
  register long x8 __asm__("x8") = 7;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
  return x0;
}

#endif

#endif

