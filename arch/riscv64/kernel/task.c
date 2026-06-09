#include <riscv64/task.h>
#include <elf.h>
#include <clib.h>
#include <types.h>

extern void ns16550_puts(const char* s);
extern void print_hex(uint64_t val);
extern void thread_entry_wrapper(void);
extern void userspace_entry_wrapper(void);
extern void thread_exit(void);

static Thread threads[MAX_THREADS];
static uint8_t thread_stacks[MAX_THREADS][STACK_SIZE] __attribute__((aligned(16)));
static Thread* current_thread = NULL;

extern uint64_t boot_pg_dir[];

// SV39 Page Tables for MAX_THREADS
// Root is Level 2 (1 table per thread)
static uint64_t thread_l2_tables[MAX_THREADS][512] __attribute__((aligned(4096)));
// Middle is Level 1 (1 table per thread)
static uint64_t thread_l1_tables[MAX_THREADS][512] __attribute__((aligned(4096)));
// Leaf is Level 0 (4 tables per thread to cover 8MB userspace virtual memory)
static uint64_t thread_l0_tables[MAX_THREADS][4][512] __attribute__((aligned(4096)));

#define USER_CODE_SIZE 0x200000 // 2MB
static uint8_t user_code_pages[MAX_THREADS][USER_CODE_SIZE] __attribute__((aligned(4096)));

#define USER_STACK_SIZE 0x20000 // 128KB
static uint8_t user_stack_pages[MAX_THREADS][USER_STACK_SIZE] __attribute__((aligned(4096)));

// Page table flags for Sv39
#define PTE_V (1 << 0)
#define PTE_R (1 << 1)
#define PTE_W (1 << 2)
#define PTE_X (1 << 3)
#define PTE_U (1 << 4)
#define PTE_A (1 << 6)
#define PTE_D (1 << 7)

void thread_init(void) {
  int i;
  for (i = 0; i < MAX_THREADS; i++) {
    threads[i].state = THREAD_STATE_FREE;
    threads[i].tid = i;
    threads[i].stack_base = thread_stacks[i];
    threads[i].stack_size = STACK_SIZE;
    
    // Default kernel thread satp: Mode=Sv39, PPN of boot_pg_dir
    threads[i].pg_dir_phys = (8ULL << 60) | ((uint64_t)boot_pg_dir >> 12);
  }

  // Thread 0 represents the main boot thread
  threads[0].state = THREAD_STATE_RUNNING;
  current_thread = &threads[0];

  ns16550_puts("Threads initialized. Main thread tid=0.\n");
}

int thread_create(void (*entry)(void)) {
  int i;
  // TODO: Disable interrupts when GIC/PLIC is implemented
  for (i = 1; i < MAX_THREADS; i++) {
    if (threads[i].state == THREAD_STATE_FREE) {
      Thread* t = &threads[i];

      // Kernel threads use boot page table
      t->pg_dir_phys = (8ULL << 60) | ((uint64_t)boot_pg_dir >> 12);

      // Setup fake context on stack
      uint8_t* stk_top = (uint8_t*)t->stack_base + t->stack_size;
      stk_top -= sizeof(CpuContext);
      CpuContext* ctx = (CpuContext*)stk_top;

      CbMemSet((int8_t*)ctx, 0, sizeof(CpuContext));

      ctx->s1 = (uint64_t)entry;                // Used by thread_entry_wrapper to jump to
      ctx->ra = (uint64_t)thread_entry_wrapper; // Return address
      ctx->sp = (uint64_t)stk_top;
      ctx->s0 = (uint64_t)stk_top;              // Frame pointer

      t->context = *ctx;
      t->state = THREAD_STATE_READY;

      ns16550_puts("Created thread tid=");
      print_hex(t->tid);
      ns16550_puts(" entry=");
      print_hex((uint64_t)entry);
      ns16550_puts("\n");

      return t->tid;
    }
  }
  return -1;
}

void thread_exit(void) {
  // TODO: Disable interrupts
  current_thread->state = THREAD_STATE_FREE;
  ns16550_puts("Thread ");
  print_hex(current_thread->tid);
  ns16550_puts(" exited.\n");
  schedule();
  while (1);
}

void schedule(void) {
  int i;
  int curr_idx = current_thread->tid;
  int next_idx;

  // TODO: Disable interrupts

retry:
  next_idx = -1;
  for (i = 1; i <= MAX_THREADS; i++) {
    int idx = (curr_idx + i) % MAX_THREADS;
    if (threads[idx].state == THREAD_STATE_READY) {
      next_idx = idx;
      break;
    }
  }

  if (next_idx != -1) {
    Thread* prev = current_thread;
    Thread* next = &threads[next_idx];

    if (prev->state == THREAD_STATE_RUNNING) {
      prev->state = THREAD_STATE_READY;
    }
    next->state = THREAD_STATE_RUNNING;
    current_thread = next;

    // Switch page tables (write to satp)
    uint64_t next_satp = next->pg_dir_phys;
    __asm__ volatile(
        "csrw satp, %0\n"
        "sfence.vma\n" ::"r"(next_satp)
        : "memory");

    cpu_switch_to(&prev->context, &next->context);
  } else {
    if (current_thread->state == THREAD_STATE_RUNNING) {
      return;
    }
    // Idle loop (wait for interrupt)
    // TODO: Enable interrupts, wfi, Disable interrupts
    __asm__ volatile("wfi");
    goto retry;
  }
}

void map_page_thread(Thread* t, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
  // Retrieve root table pointer from satp PPN
  uint64_t root_phys = (t->pg_dir_phys & ((1ULL << 44) - 1)) << 12;
  uint64_t* l2 = (uint64_t*)root_phys; 
  
  uint32_t l1_idx = (vaddr >> 21) & 0x1FF; // VPN[1]
  uint32_t l0_idx = (vaddr >> 12) & 0x1FF; // VPN[0]

  // SV39 root has VPN[2] at bits 38:30. For userspace (0-1GB) it is always index 0.
  // So l2[0] points to the middle table (Level 1)
  if ((l2[0] & PTE_V) == 0) {
    // If invalid, initialize pointer to Level 1 table
    uint64_t* l1_table = thread_l1_tables[t->tid];
    l2[0] = (((uint64_t)l1_table & ~0xFFF) >> 12 << 10) | PTE_V;
  }
  
  uint64_t* l1 = (uint64_t*)(((l2[0] >> 10) & ((1ULL << 44) - 1)) << 12);
  
  if (l1_idx < 4) {
    if ((l1[l1_idx] & PTE_V) == 0) {
      uint64_t* l0_table = thread_l0_tables[t->tid][l1_idx];
      l1[l1_idx] = (((uint64_t)l0_table & ~0xFFF) >> 12 << 10) | PTE_V;
    }
    uint64_t* l0 = (uint64_t*)(((l1[l1_idx] >> 10) & ((1ULL << 44) - 1)) << 12);
    l0[l0_idx] = (((paddr & ~0xFFF) >> 12) << 10) | flags;
  }
}

int thread_create_userspace(const unsigned char* bin, uint32_t size) {
  int i;
  // TODO: Disable interrupts
  for (i = 1; i < MAX_THREADS; i++) {
    if (threads[i].state == THREAD_STATE_FREE) {
      Thread* t = &threads[i];

      // Initialize Sv39 page tables for userspace thread
      uint64_t* l2 = thread_l2_tables[i]; // Root
      uint64_t* l1 = thread_l1_tables[i]; // Middle
      
      CbMemSet((int8_t*)l2, 0, 4096);
      CbMemSet((int8_t*)l1, 0, 4096);
      
      // L2[0] -> L1
      l2[0] = (((uint64_t)l1 & ~0xFFF) >> 12 << 10) | PTE_V;
      // L2[2] -> copy RAM block mapping from boot_pg_dir to allow kernel access in S-mode
      l2[2] = boot_pg_dir[2];

      t->pg_dir_phys = (8ULL << 60) | ((uint64_t)l2 >> 12);

      CbMemSet((int8_t*)user_code_pages[i], 0, USER_CODE_SIZE);
      CbMemSet((int8_t*)user_stack_pages[i], 0, USER_STACK_SIZE);

      // Sv39 Leaf PTE flags for User Code (RX): V | R | X | U | A
      uint64_t code_flags = PTE_V | PTE_R | PTE_X | PTE_U | PTE_A;
      
      Elf64_Ehdr* ehdr = (Elf64_Ehdr*)bin;
      uint64_t entry_point = 0x00100000;
      
      if (ehdr->e_ident[0] == 0x7f &&
          ehdr->e_ident[1] == 'E' &&
          ehdr->e_ident[2] == 'L' &&
          ehdr->e_ident[3] == 'F') {
          
          entry_point = ehdr->e_entry;
          
          Elf64_Phdr* phdr = (Elf64_Phdr*)(bin + ehdr->e_phoff);
          int j;
          for (j = 0; j < ehdr->e_phnum; j++) {
            if (phdr[j].p_type == PT_LOAD) {
              uint64_t vaddr = phdr[j].p_vaddr;
              uint64_t memsz = phdr[j].p_memsz;
              uint64_t filesz = phdr[j].p_filesz;
              uint64_t offset = phdr[j].p_offset;

              uint64_t start_page = vaddr & ~0xFFF;
              uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
              uint64_t curr_page;

              for (curr_page = start_page; curr_page < end_page; curr_page += 4096) {
                uint64_t phys_offset = curr_page - start_page;
                
                // Set page table entry
                map_page_thread(t, curr_page, (uint64_t)user_code_pages[i] + phys_offset, code_flags);
                
                // Copy data if available
                if (phys_offset < filesz) {
                  uint64_t copy_size = filesz - phys_offset;
                  if (copy_size > 4096) copy_size = 4096;
                  CbMemCpy(user_code_pages[i] + phys_offset, bin + offset + phys_offset, copy_size);
                }
              }
            }
          }
      } else {
          // Raw binary
          CbMemCpy(user_code_pages[i], bin, size);
          uint32_t num_pages = USER_CODE_SIZE / 4096;
          uint32_t p;
          for (p = 0; p < num_pages; p++) {
            uint64_t vaddr = 0x00100000 + p * 4096;
            uint64_t paddr = (uint64_t)user_code_pages[i] + p * 4096;
            map_page_thread(t, vaddr, paddr, code_flags);
          }
      }

      // Map User Stack: V | R | W | U | A | D
      uint64_t stack_flags = PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D;
      uint32_t page_idx;
      for (page_idx = 0; page_idx < (USER_STACK_SIZE / 4096); page_idx++) {
          map_page_thread(t, 0x00800000 - USER_STACK_SIZE + (page_idx * 4096), (uint64_t)user_stack_pages[i] + (page_idx * 4096), stack_flags);
      }

      uint8_t* stk_top = (uint8_t*)t->stack_base + t->stack_size;
      stk_top -= sizeof(CpuContext);
      CpuContext* ctx = (CpuContext*)stk_top;

      CbMemSet((int8_t*)ctx, 0, sizeof(CpuContext));

      // Registers for userspace_entry_wrapper:
      ctx->s1 = entry_point;                    // sepc value
      ctx->ra = (uint64_t)userspace_entry_wrapper; // Switch entry wrapper
      ctx->sp = (uint64_t)stk_top;
      ctx->s0 = (uint64_t)stk_top;
      
      uint64_t user_sp = 0x00800000 - 0x100;
      ctx->s2 = user_sp;                        // User SP
      ctx->s3 = (uint64_t)t->stack_base + t->stack_size; // Kernel Stack Top (for sscratch)

      // Setup argc, argv stubs on user stack
      uint64_t* ustack = (uint64_t*)(user_stack_pages[i] + USER_STACK_SIZE - 0x100);
      ustack[0] = 1;                            // argc
      ustack[1] = user_sp + 0x10;               // argv[0]
      ustack[2] = 0;                            // NULL
      CbMemCpy((char*)(ustack + 3), "shell", 6);

      t->context = *ctx;
      t->state = THREAD_STATE_READY;

      ns16550_puts("Created userspace thread tid=");
      print_hex(t->tid);
      ns16550_puts(" entry=");
      print_hex(entry_point);
      ns16550_puts("\n");

      return t->tid;
    }
  }
  return -1;
}

uint32_t thread_get_current_tid(void) {
  return current_thread->tid;
}

void thread_set_current_regs(RISCV64Registers* regs) {
  current_thread->regs = regs;
}

// Stubs for IPC/MMIO mapping
int thread_ipc_send(uint32_t dest, void* buf, uint32_t size) { return -1; }
int thread_ipc_recv(uint32_t src, void* buf, uint32_t size) { return -1; }
void* thread_map_mmio(uint64_t phys_addr) { return NULL; }
void* thread_map_fb(void) { return NULL; }
