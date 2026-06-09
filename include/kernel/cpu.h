#ifndef __KERNEL_CPU_H__
#define __KERNEL_CPU_H__

#include <types.h>

// Forward declaration of struct _Thread to avoid circular dependency
struct _Thread;

typedef struct CpuLocal {
    uint64_t user_sp;               // offset 0
    uint64_t kernel_stack;          // offset 8
    uint64_t hartid;                // offset 16
    struct _Thread* current_thread; // offset 24
    uint64_t temp_regs[4];          // offset 32, 40, 48, 56
    struct _Thread* last_prev;      // offset 64
} CpuLocal;

#define MAX_CPUS 4
extern CpuLocal cpus[MAX_CPUS];

static inline uint64_t get_cpu_id(void) {
    uint64_t hartid;
    __asm__ volatile("mv %0, tp" : "=r"(hartid));
    return hartid;
}

static inline CpuLocal* get_cpu_local(void) {
    // If tp points to cpus[hartid], we can just return tp.
    // Wait, if tp contains hartid instead of pointer, we can do cpus[tp].
    // Let's store hartid in tp for simplicity, and index cpus[tp].
    // Actually, storing pointer in tp is faster, but storing hartid is easier to debug.
    // Let's store hartid in tp.
    uint64_t hartid = get_cpu_id();
    return &cpus[hartid];
}

#endif // __KERNEL_CPU_H__
