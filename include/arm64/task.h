#ifndef __ARM64_TASK_H__
#define __ARM64_TASK_H__

#include <types.h>
#include <arm64/platform.h>

typedef enum {
    THREAD_STATE_FREE = 0,
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKED,
} ThreadState;

typedef struct {
    u64 x19;
    u64 x20;
    u64 x21;
    u64 x22;
    u64 x23;
    u64 x24;
    u64 x25;
    u64 x26;
    u64 x27;
    u64 x28;
    u64 fp; // x29
    u64 lr; // x30
    u64 sp;
} CpuContext;

typedef struct _Thread {
    CpuContext context;
    ThreadState state;
    u32 tid;
    void *stack_base;
    u32 stack_size;
    u64 pg_dir_phys;
    ARM64Registers *regs; // Saved registers pointer
    
    // IPC State
    u32 ipc_partner;
    void *ipc_buf;
    u32 ipc_size;
} Thread;

#define MAX_THREADS 8
#define STACK_SIZE 4096
#define ANY_THREAD 0xFFFFFFFF
#define IPC_BLOCKED -2

void thread_init(void);
int thread_create(void (*entry)(void));
int thread_create_userspace(const unsigned char *bin, u32 size);
void cpu_switch_to(CpuContext *current, CpuContext *next);
void schedule(void);
u32 thread_get_current_tid(void);
void thread_set_current_regs(ARM64Registers *regs);
int thread_ipc_send(u32 dest, void *buf, u32 size);
int thread_ipc_recv(u32 src, void *buf, u32 size);
void* thread_map_mmio(u64 phys_addr);

#endif // __ARM64_TASK_H__
