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

static inline int sys_spawn(const void* buf, int size, const char* arg) {
  register long a0 __asm__("a0") = (long)buf;
  register long a1 __asm__("a1") = size;
  register long a2 __asm__("a2") = (long)arg;
  register long a7 __asm__("a7") = 7;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
  return a0;
}

static inline void* sys_mmap(void* addr, unsigned long length, int prot, int flags, int fd, unsigned long offset) {
  register long a0 __asm__("a0") = (long)addr;
  register long a1 __asm__("a1") = length;
  register long a2 __asm__("a2") = prot;
  register long a3 __asm__("a3") = flags;
  register long a4 __asm__("a4") = fd;
  register long a5 __asm__("a5") = offset;
  register long a7 __asm__("a7") = 222;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a7) : "memory");
  return (void*)a0;
}

static inline int sys_munmap(void* addr, unsigned long length) {
  register long a0 __asm__("a0") = (long)addr;
  register long a1 __asm__("a1") = length;
  register long a7 __asm__("a7") = 215;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
  return a0;
}

static inline int sys_mprotect(void* addr, unsigned long length, int prot) {
  register long a0 __asm__("a0") = (long)addr;
  register long a1 __asm__("a1") = length;
  register long a2 __asm__("a2") = prot;
  register long a7 __asm__("a7") = 226;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
  return a0;
}

static inline int sys_read(int fd, void* buf, int len) {
  register long a0 __asm__("a0") = fd;
  register long a1 __asm__("a1") = (long)buf;
  register long a2 __asm__("a2") = len;
  register long a7 __asm__("a7") = 63;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
  return a0;
}

static inline int sys_write(int fd, const void* buf, int len) {
  register long a0 __asm__("a0") = fd;
  register long a1 __asm__("a1") = (long)buf;
  register long a2 __asm__("a2") = len;
  register long a7 __asm__("a7") = 64;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
  return a0;
}

static inline int sys_openat(int dfd, const char* filename, int flags) {
  register long a0 __asm__("a0") = dfd;
  register long a1 __asm__("a1") = (long)filename;
  register long a2 __asm__("a2") = flags;
  register long a7 __asm__("a7") = 56;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
  return a0;
}

static inline int sys_close(int fd) {
  register long a0 __asm__("a0") = fd;
  register long a7 __asm__("a7") = 57;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
  return a0;
}

static inline void sys_exit(int status) {
  register long a0 __asm__("a0") = status;
  register long a7 __asm__("a7") = 93;
  __asm__ volatile("ecall" :: "r"(a0), "r"(a7) : "memory");
  while(1);
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

static inline int sys_spawn(const void* buf, int size, const char* arg) {
  register long x0 __asm__("x0") = (long)buf;
  register long x1 __asm__("x1") = size;
  register long x2 __asm__("x2") = (long)arg;
  register long x8 __asm__("x8") = 7;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
  return x0;
}

static inline void* sys_mmap(void* addr, unsigned long length, int prot, int flags, int fd, unsigned long offset) {
  register long x0 __asm__("x0") = (long)addr;
  register long x1 __asm__("x1") = length;
  register long x2 __asm__("x2") = prot;
  register long x3 __asm__("x3") = flags;
  register long x4 __asm__("x4") = fd;
  register long x5 __asm__("x5") = offset;
  register long x8 __asm__("x8") = 222;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8) : "memory");
  return (void*)x0;
}

static inline int sys_munmap(void* addr, unsigned long length) {
  register long x0 __asm__("x0") = (long)addr;
  register long x1 __asm__("x1") = length;
  register long x8 __asm__("x8") = 215;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
  return x0;
}

static inline int sys_mprotect(void* addr, unsigned long length, int prot) {
  register long x0 __asm__("x0") = (long)addr;
  register long x1 __asm__("x1") = length;
  register long x2 __asm__("x2") = prot;
  register long x8 __asm__("x8") = 226;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
  return x0;
}

static inline int sys_read(int fd, void* buf, int len) {
  register long x0 __asm__("x0") = fd;
  register long x1 __asm__("x1") = (long)buf;
  register long x2 __asm__("x2") = len;
  register long x8 __asm__("x8") = 63;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
  return x0;
}

static inline int sys_write(int fd, const void* buf, int len) {
  register long x0 __asm__("x0") = fd;
  register long x1 __asm__("x1") = (long)buf;
  register long x2 __asm__("x2") = len;
  register long x8 __asm__("x8") = 64;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
  return x0;
}

static inline int sys_openat(int dfd, const char* filename, int flags) {
  register long x0 __asm__("x0") = dfd;
  register long x1 __asm__("x1") = (long)filename;
  register long x2 __asm__("x2") = flags;
  register long x8 __asm__("x8") = 56;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
  return x0;
}

static inline int sys_close(int fd) {
  register long x0 __asm__("x0") = fd;
  register long x8 __asm__("x8") = 57;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
  return x0;
}

static inline void sys_exit(int status) {
  register long x0 __asm__("x0") = status;
  register long x8 __asm__("x8") = 93;
  __asm__ volatile("svc #0" :: "r"(x0), "r"(x8) : "memory");
  while(1);
}

#endif

#endif

