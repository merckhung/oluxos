#ifndef __ARM64_TASK_H__
#define __ARM64_TASK_H__

#include <arm64/platform.h>
#include <types.h>

typedef enum {
  THREAD_STATE_FREE = 0,
  THREAD_STATE_READY,
  THREAD_STATE_RUNNING,
  THREAD_STATE_BLOCKED,
} ThreadState;

typedef struct {
  uint64_t x19;
  uint64_t x20;
  uint64_t x21;
  uint64_t x22;
  uint64_t x23;
  uint64_t x24;
  uint64_t x25;
  uint64_t x26;
  uint64_t x27;
  uint64_t x28;
  uint64_t fp;  // x29
  uint64_t lr;  // x30
  uint64_t sp;
} CpuContext;

typedef struct {
  int used;
  uint32_t srv_tid;
  int handle;
  uint64_t offset;
  char path[64];
} KernelFdEntry;

#define MAX_KERNEL_FDS 16

typedef struct _Thread {
  CpuContext context;
  ThreadState state;
  uint32_t tid;
  void* stack_base;
  uint32_t stack_size;
  uint64_t pg_dir_phys;
  uint64_t brk;
  ARM64Registers* regs;  // Saved registers pointer

  // IPC State
  uint32_t ipc_partner;
  void* ipc_buf;
  uint32_t ipc_size;

  // POSIX FDs
  KernelFdEntry fds[MAX_KERNEL_FDS];
} Thread;

#define MAX_THREADS 8
#define STACK_SIZE 4096
#define ANY_THREAD 0xFFFFFFFF
#define UART_HARDWARE 0xFFFFFFFE
#define IPC_BLOCKED -2

void thread_init(void);
int thread_create(void (*entry)(void));
int thread_create_userspace(const unsigned char* bin, uint32_t size, const char* arg);
void cpu_switch_to(CpuContext* current, CpuContext* next);
void schedule(void);
uint32_t thread_get_current_tid(void);
void thread_set_current_regs(ARM64Registers* regs);
int thread_ipc_send(uint32_t dest, void* buf, uint32_t size);
int thread_ipc_recv(uint32_t src, void* buf, uint32_t size);
int thread_block_on_uart(void* buf, uint32_t len);
void* thread_map_mmio(uint64_t phys_addr);
void* thread_map_fb(void);
extern Thread* current_thread;

#endif  // __ARM64_TASK_H__
