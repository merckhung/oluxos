#ifndef __RISCV64_TASK_H__
#define __RISCV64_TASK_H__

#include <types.h>

typedef enum {
  THREAD_STATE_FREE = 0,
  THREAD_STATE_READY,
  THREAD_STATE_RUNNING,
  THREAD_STATE_BLOCKED,
} ThreadState;

// RISC-V Callee-saved registers + SP + RA
typedef struct {
  uint64_t ra;   // return address (x1)
  uint64_t sp;   // stack pointer (x2)
  uint64_t s0;   // frame pointer / saved register 0 (x8)
  uint64_t s1;   // saved register 1 (x9)
  uint64_t s2;   // saved register 2 (x18)
  uint64_t s3;   // saved register 3 (x19)
  uint64_t s4;   // saved register 4 (x20)
  uint64_t s5;   // saved register 5 (x21)
  uint64_t s6;   // saved register 6 (x22)
  uint64_t s7;   // saved register 7 (x23)
  uint64_t s8;   // saved register 8 (x24)
  uint64_t s9;   // saved register 9 (x25)
  uint64_t s10;  // saved register 10 (x26)
  uint64_t s11;  // saved register 11 (x27)
} CpuContext;

// RISC-V Userspace saved registers for exception/trap frame
typedef struct {
  uint64_t gpr[32]; // general purpose registers x0-x31
  uint64_t sepc;    // supervisor exception program counter
  uint64_t sstatus; // supervisor status register
  uint64_t sbadaddr; // supervisor bad address (stval)
  uint64_t scause;   // supervisor cause
} RISCV64Registers;

typedef struct _Thread {
  CpuContext context;
  ThreadState state;
  uint32_t tid;
  void* stack_base;
  uint32_t stack_size;
  uint64_t pg_dir_phys; // Physical address of root page table (satp value)
  RISCV64Registers* regs; // Saved registers pointer for traps

  // IPC State
  uint32_t ipc_partner;
  void* ipc_buf;
  uint32_t ipc_size;
} Thread;

#define MAX_THREADS 8
#define STACK_SIZE 4096
#define ANY_THREAD 0xFFFFFFFF
#define IPC_BLOCKED -2

void thread_init(void);
int thread_create(void (*entry)(void));
int thread_create_userspace(const unsigned char* bin, uint32_t size);
void cpu_switch_to(CpuContext* current, CpuContext* next);
void schedule(void);
uint32_t thread_get_current_tid(void);
void thread_set_current_regs(RISCV64Registers* regs);
int thread_ipc_send(uint32_t dest, void* buf, uint32_t size);
int thread_ipc_recv(uint32_t src, void* buf, uint32_t size);
void* thread_map_mmio(uint64_t phys_addr);
void* thread_map_fb(void);

#endif // __RISCV64_TASK_H__
