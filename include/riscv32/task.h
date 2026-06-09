#ifndef __RISCV32_TASK_H__
#define __RISCV32_TASK_H__

#include <types.h>

// Page table flags for Sv32
#define PTE_V (1 << 0)
#define PTE_R (1 << 1)
#define PTE_W (1 << 2)
#define PTE_X (1 << 3)
#define PTE_U (1 << 4)
#define PTE_A (1 << 6)
#define PTE_D (1 << 7)

typedef enum {
  THREAD_STATE_FREE = 0,
  THREAD_STATE_READY,
  THREAD_STATE_RUNNING,
  THREAD_STATE_BLOCKED,
} ThreadState;

// RISC-V 32-bit Callee-saved registers + SP + RA
typedef struct {
  uint32_t ra;   // return address (x1)
  uint32_t sp;   // stack pointer (x2)
  uint32_t s0;   // frame pointer / saved register 0 (x8)
  uint32_t s1;   // saved register 1 (x9)
  uint32_t s2;   // saved register 2 (x18)
  uint32_t s3;   // saved register 3 (x19)
  uint32_t s4;   // saved register 4 (x20)
  uint32_t s5;   // saved register 5 (x21)
  uint32_t s6;   // saved register 6 (x22)
  uint32_t s7;   // saved register 7 (x23)
  uint32_t s8;   // saved register 8 (x24)
  uint32_t s9;   // saved register 9 (x25)
  uint32_t s10;  // saved register 10 (x26)
  uint32_t s11;  // saved register 11 (x27)
} CpuContext;

// RISC-V 32-bit Userspace saved registers for exception/trap frame
typedef struct {
  uint32_t gpr[32]; // general purpose registers x0-x31
  uint32_t sepc;    // supervisor exception program counter
  uint32_t sstatus; // supervisor status register
  uint32_t sbadaddr; // supervisor bad address (stval)
  uint32_t scause;   // supervisor cause
} RISCV32Registers;

typedef struct _Thread {
  CpuContext context;
  ThreadState state;
  uint32_t tid;
  void* stack_base;
  uint32_t stack_size;
  uint32_t pg_dir_phys; // Physical address of root page table (satp value)
  RISCV32Registers* regs; // Saved registers pointer for traps

  // IPC State
  uint32_t ipc_partner;
  void* ipc_buf;
  uint32_t ipc_size;
} Thread;

#define MAX_THREADS 4
#define STACK_SIZE 4096
#define ANY_THREAD 0xFFFFFFFF
#define IPC_BLOCKED -2

void thread_init(void);
int thread_create(void (*entry)(void));
int thread_create_userspace(const unsigned char* bin, uint32_t size, const char* arg);
void cpu_switch_to(CpuContext* current, CpuContext* next);
void schedule(void);
uint32_t thread_get_current_tid(void);
void thread_set_current_regs(RISCV32Registers* regs);
int thread_ipc_send(uint32_t dest, void* buf, uint32_t size);
int thread_ipc_recv(uint32_t src, void* buf, uint32_t size);
void* thread_map_mmio(uint32_t phys_addr);
void* thread_map_fb(void);

#endif // __RISCV32_TASK_H__
