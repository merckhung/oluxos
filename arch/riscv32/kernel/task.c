#include <riscv32/task.h>
#include <riscv32/interrupt.h>
#include <elf.h>
#include <clib.h>
#include <types.h>

extern void ns16550_puts(const char* s);
extern void print_hex(uint32_t val);
extern void thread_entry_wrapper(void);
extern void userspace_entry_wrapper(void);
extern void thread_exit(void);

static Thread threads[MAX_THREADS];
static uint8_t thread_stacks[MAX_THREADS][STACK_SIZE] __attribute__((aligned(16)));
static Thread* current_thread = NULL;

extern uint32_t boot_pg_dir[];

// SV32 Page Tables for MAX_THREADS
// Root is Level 1 (1 table per thread, 1024 entries of 4 bytes = 4KB)
static uint32_t thread_l1_tables[MAX_THREADS][1024] __attribute__((aligned(4096)));
// Leaf is Level 0 (4 tables per thread to cover 16MB userspace virtual memory)
static uint32_t thread_l0_tables[MAX_THREADS][4][1024] __attribute__((aligned(4096)));

#define USER_CODE_SIZE 0x80000 // 512KB
static uint8_t user_code_pages[MAX_THREADS][USER_CODE_SIZE] __attribute__((aligned(4096)));

#define USER_STACK_SIZE 0x20000 // 128KB
static uint8_t user_stack_pages[MAX_THREADS][USER_STACK_SIZE] __attribute__((aligned(4096)));


void thread_init(void) {
  int i;
  for (i = 0; i < MAX_THREADS; i++) {
    threads[i].state = THREAD_STATE_FREE;
    threads[i].tid = i;
    threads[i].stack_base = thread_stacks[i];
    threads[i].stack_size = STACK_SIZE;
    
    // Default kernel thread satp: Mode=Sv32 (1 << 31), PPN of boot_pg_dir
    threads[i].pg_dir_phys = (1UL << 31) | ((uint32_t)boot_pg_dir >> 12);
  }

  // Thread 0 represents the main boot thread
  threads[0].state = THREAD_STATE_RUNNING;
  current_thread = &threads[0];

  ns16550_puts("Threads initialized. Main thread tid=0.\n");
}

int thread_create(void (*entry)(void)) {
  int i;
  IntDisable();
  for (i = 1; i < MAX_THREADS; i++) {
    if (threads[i].state == THREAD_STATE_FREE) {
      Thread* t = &threads[i];

      // Kernel threads use boot page table
      t->pg_dir_phys = (1UL << 31) | ((uint32_t)boot_pg_dir >> 12);

      // Setup fake context on stack
      uint8_t* stk_top = (uint8_t*)t->stack_base + t->stack_size;
      stk_top -= sizeof(CpuContext);
      CpuContext* ctx = (CpuContext*)stk_top;

      CbMemSet((int8_t*)ctx, 0, sizeof(CpuContext));

      ctx->s1 = (uint32_t)entry;                // Used by thread_entry_wrapper to jump to
      ctx->ra = (uint32_t)thread_entry_wrapper; // Return address
      ctx->sp = (uint32_t)stk_top;
      ctx->s0 = (uint32_t)stk_top;              // Frame pointer

      t->context = *ctx;
      t->state = THREAD_STATE_READY;

      ns16550_puts("Created thread tid=");
      print_hex(t->tid);
      ns16550_puts(" entry=");
      print_hex((uint32_t)entry);
      ns16550_puts("\n");

      IntEnable();
      return t->tid;
    }
  }
  IntEnable();
  return -1;
}

void thread_exit(void) {
  IntDisable();
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

  IntDisable();

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
    uint32_t next_satp = next->pg_dir_phys;
    __asm__ volatile(
        "csrw satp, %0\n"
        "sfence.vma\n" ::"r"(next_satp)
        : "memory");

    cpu_switch_to(&prev->context, &next->context);

  } else {
    if (current_thread->state == THREAD_STATE_RUNNING) {
      IntEnable();
      return;
    }
    ns16550_puts("Sched: idle\n");
    IntEnable();
    __asm__ volatile("wfi");
    IntDisable();
    goto retry;
  }
  IntEnable();
}

void map_page_thread(Thread* t, uint32_t vaddr, uint32_t paddr, uint32_t flags) {
  uint32_t root_phys = (t->pg_dir_phys & ((1U << 22) - 1)) << 12;
  uint32_t* l1 = (uint32_t*)root_phys; 
  
  uint32_t l1_idx = (vaddr >> 22) & 0x3FF; // VPN[1]
  uint32_t l0_idx = (vaddr >> 12) & 0x3FF; // VPN[0]

  if (l1_idx < 4) { // Limit to 16MB userspace
    if ((l1[l1_idx] & PTE_V) == 0) {
      uint32_t* l0_table = thread_l0_tables[t->tid][l1_idx];
      l1[l1_idx] = (((uint32_t)l0_table & ~0xFFF) >> 12 << 10) | PTE_V;
    }
    uint32_t* l0 = (uint32_t*)(((l1[l1_idx] >> 10) & ((1U << 22) - 1)) << 12);
    l0[l0_idx] = (((paddr & ~0xFFF) >> 12) << 10) | flags;
  }
}

int thread_create_userspace(const unsigned char* bin, uint32_t size) {
  int i;
  IntDisable();
  for (i = 1; i < MAX_THREADS; i++) {
    if (threads[i].state == THREAD_STATE_FREE) {
      Thread* t = &threads[i];

      // Initialize Sv32 page tables for userspace thread
      uint32_t* l1 = thread_l1_tables[i]; // Root
      
      // Copy boot page table to inherit kernel mappings (RAM, UART)
      CbMemCpy((int8_t*)l1, (int8_t*)boot_pg_dir, 4096);
      
      // Zero out userspace entries (0-16MB, indices 0-3)
      l1[0] = 0;
      l1[1] = 0;
      l1[2] = 0;
      l1[3] = 0;

      t->pg_dir_phys = (1UL << 31) | ((uint32_t)l1 >> 12);

      CbMemSet((int8_t*)user_code_pages[i], 0, USER_CODE_SIZE);
      CbMemSet((int8_t*)user_stack_pages[i], 0, USER_STACK_SIZE);

      // Default flags for raw fallback (RWX)
      uint32_t code_flags = PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D;
      
      Elf32_Ehdr* ehdr = (Elf32_Ehdr*)bin;
      uint32_t entry_point = 0x00100000;
      
      if (ehdr->e_ident[0] == 0x7f &&
          ehdr->e_ident[1] == 'E' &&
          ehdr->e_ident[2] == 'L' &&
          ehdr->e_ident[3] == 'F') {
          
          entry_point = ehdr->e_entry;
          
          Elf32_Phdr* phdr = (Elf32_Phdr*)(bin + ehdr->e_phoff);
          int j;
          for (j = 0; j < ehdr->e_phnum; j++) {
            if (phdr[j].p_type == PT_LOAD) {
              uint32_t vaddr = phdr[j].p_vaddr;
              uint32_t memsz = phdr[j].p_memsz;
              uint32_t filesz = phdr[j].p_filesz;
              uint32_t offset = phdr[j].p_offset;

              uint32_t seg_flags = PTE_V | PTE_U | PTE_A;
              if (phdr[j].p_flags & PF_R) seg_flags |= PTE_R;
              if (phdr[j].p_flags & PF_W) seg_flags |= PTE_W | PTE_D;
              if (phdr[j].p_flags & PF_X) seg_flags |= PTE_X;

              uint32_t start_page = vaddr & ~0xFFF;
              uint32_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
              uint32_t curr_page;

              for (curr_page = start_page; curr_page < end_page; curr_page += 4096) {
                uint32_t phys_offset = curr_page - start_page;
                
                // Set page table entry
                map_page_thread(t, curr_page, (uint32_t)user_code_pages[i] + phys_offset, seg_flags);
                
                // Copy data if available
                if (phys_offset < filesz) {
                  uint32_t copy_size = filesz - phys_offset;
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
            uint32_t vaddr = 0x00100000 + p * 4096;
            uint32_t paddr = (uint32_t)user_code_pages[i] + p * 4096;
            map_page_thread(t, vaddr, paddr, code_flags);
          }
      }

      // Map User Stack: V | R | W | U | A | D
      uint32_t stack_flags = PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D;
      uint32_t page_idx;
      for (page_idx = 0; page_idx < (USER_STACK_SIZE / 4096); page_idx++) {
          map_page_thread(t, 0x00800000 - USER_STACK_SIZE + (page_idx * 4096), (uint32_t)user_stack_pages[i] + (page_idx * 4096), stack_flags);
      }

      uint8_t* stk_top = (uint8_t*)t->stack_base + t->stack_size;
      stk_top -= sizeof(CpuContext);
      CpuContext* ctx = (CpuContext*)stk_top;

      CbMemSet((int8_t*)ctx, 0, sizeof(CpuContext));

      // Registers for userspace_entry_wrapper:
      ctx->s1 = entry_point;                    // sepc value
      ctx->ra = (uint32_t)userspace_entry_wrapper; // Switch entry wrapper
      ctx->sp = (uint32_t)stk_top;
      ctx->s0 = (uint32_t)stk_top;
      
      uint32_t user_sp = 0x00800000 - 0x100;
      ctx->s2 = user_sp;                        // User SP
      ctx->s3 = (uint32_t)t->stack_base + t->stack_size; // Kernel Stack Top (for sscratch)

      // Setup argc, argv stubs on user stack (32-bit pointers)
      uint32_t* ustack = (uint32_t*)(user_stack_pages[i] + USER_STACK_SIZE - 0x100);
      ustack[0] = 1;                            // argc
      ustack[1] = user_sp + 0x8;                // argv[0] (argv starts at ustack[2] in 32-bit: argc(4), argv[0](4), NULL(4). Wait.
      // ustack[0] = argc (4 bytes)
      // ustack[1] = argv (points to argv[0] which is ustack[2])
      // ustack[2] = argv[0] (points to string "shell" at ustack[4])
      // ustack[3] = NULL
      // ustack[4] = "shell" (6 bytes)
      ustack[1] = user_sp + 8;                  // argv points to ustack[2]
      ustack[2] = user_sp + 16;                 // argv[0] points to "shell" (ustack[4])
      ustack[3] = 0;                            // NULL
      CbMemCpy((char*)(ustack + 4), "shell", 6);

      t->context = *ctx;
      t->state = THREAD_STATE_READY;

      ns16550_puts("Created userspace thread tid=");
      print_hex(t->tid);
      ns16550_puts(" entry=");
      print_hex(entry_point);
      ns16550_puts(" stack_base=");
      print_hex((uint32_t)t->stack_base);
      ns16550_puts(" stack_top=");
      print_hex((uint32_t)t->stack_base + t->stack_size);
      ns16550_puts("\n");

      IntEnable();
      return t->tid;
    }
  }
  IntEnable();
  return -1;
}

uint32_t thread_get_current_tid(void) {
  return current_thread->tid;
}

void thread_set_current_regs(RISCV32Registers* regs) {
  current_thread->regs = regs;
}

uint32_t translate_user_va(Thread* t, uint32_t va) {
  uint32_t root_phys = (t->pg_dir_phys & ((1U << 22) - 1)) << 12;
  uint32_t* l1 = (uint32_t*)root_phys;

  uint32_t l1_idx = (va >> 22) & 0x3FF;
  if (l1_idx >= 4) return 0; // limit userspace to 16MB

  uint32_t l1_entry = l1[l1_idx];
  if ((l1_entry & PTE_V) == 0) return 0;

  if (l1_entry & (PTE_R | PTE_W | PTE_X)) {
    uint32_t ppn = (l1_entry >> 10) & ((1U << 22) - 1);
    return (ppn << 12) | (va & 0x3FFFFF);
  }

  uint32_t* l0 = (uint32_t*)(((l1_entry >> 10) & ((1U << 22) - 1)) << 12);
  uint32_t l0_idx = (va >> 12) & 0x3FF;
  uint32_t l0_entry = l0[l0_idx];
  if ((l0_entry & PTE_V) == 0) return 0;

  uint32_t ppn = (l0_entry >> 10) & ((1U << 22) - 1);
  return (ppn << 12) | (va & 0xFFF);
}

int thread_ipc_send(uint32_t dest, void* buf, uint32_t size) {
  IntDisable();

  if (dest == current_thread->tid || dest >= MAX_THREADS ||
      threads[dest].state == THREAD_STATE_FREE) {
    IntEnable();
    return -1;
  }

  Thread* dest_thread = &threads[dest];

  // Check if receiver is already waiting for us
  if (dest_thread->state == THREAD_STATE_BLOCKED &&
      (dest_thread->ipc_partner == current_thread->tid ||
       dest_thread->ipc_partner == ANY_THREAD)) {
    uint32_t copy_size =
        size < dest_thread->ipc_size ? size : dest_thread->ipc_size;

    uint32_t src_phys = translate_user_va(current_thread, (uint32_t)buf);
    uint32_t dest_phys =
        translate_user_va(dest_thread, (uint32_t)dest_thread->ipc_buf);

    if (src_phys == 0 || dest_phys == 0) {
      ns16550_puts("IPC send: Translation failed. src_phys=");
      print_hex(src_phys);
      ns16550_puts(" dest_phys=");
      print_hex(dest_phys);
      ns16550_puts("\n");
      IntEnable();
      return -1;
    }

    CbMemCpy((void*)dest_phys, (void*)src_phys, copy_size);

    dest_thread->regs->gpr[10] = copy_size; // a0 (x10) is return value
    dest_thread->state = THREAD_STATE_READY;

    IntEnable();
    return 0;
  }

  current_thread->state = THREAD_STATE_BLOCKED;
  current_thread->ipc_partner = dest;
  current_thread->ipc_buf = buf;
  current_thread->ipc_size = size;

  schedule();

  return IPC_BLOCKED;
}

int thread_ipc_recv(uint32_t src, void* buf, uint32_t size) {
  IntDisable();

  if (src != ANY_THREAD) {
    if (src == current_thread->tid || src >= MAX_THREADS ||
        threads[src].state == THREAD_STATE_FREE) {
      IntEnable();
      return -1;
    }
  }

  Thread* sender_thread = NULL;

  if (src == ANY_THREAD) {
    int i;
    for (i = 1; i < MAX_THREADS; i++) {
      if (threads[i].state == THREAD_STATE_BLOCKED &&
          threads[i].ipc_partner == current_thread->tid) {
        sender_thread = &threads[i];
        break;
      }
    }
  } else {
    Thread* t = &threads[src];
    if (t->state == THREAD_STATE_BLOCKED &&
        t->ipc_partner == current_thread->tid) {
      sender_thread = t;
    }
  }

  if (sender_thread != NULL) {
    uint32_t copy_size =
        size < sender_thread->ipc_size ? size : sender_thread->ipc_size;

    uint32_t src_phys =
        translate_user_va(sender_thread, (uint32_t)sender_thread->ipc_buf);
    uint32_t dest_phys = translate_user_va(current_thread, (uint32_t)buf);

    if (src_phys == 0 || dest_phys == 0) {
      ns16550_puts("IPC recv: Translation failed\n");
      IntEnable();
      return -1;
    }

    CbMemCpy((void*)dest_phys, (void*)src_phys, copy_size);

    sender_thread->regs->gpr[10] = 0; // a0 (x10) is return value for sender
    sender_thread->state = THREAD_STATE_READY;

    IntEnable();
    return copy_size;
  }

  current_thread->state = THREAD_STATE_BLOCKED;
  current_thread->ipc_partner = src;
  current_thread->ipc_buf = buf;
  current_thread->ipc_size = size;

  schedule();

  return IPC_BLOCKED;
}

void* thread_map_mmio(uint32_t phys_addr) {
  // QEMU Virt NS16550 UART (0x10000000)
  if (phys_addr == 0x10000000) {
    uint32_t root_phys = (current_thread->pg_dir_phys & ((1U << 22) - 1)) << 12;
    uint32_t* l1 = (uint32_t*)root_phys;

    // Map 4MB leaf block in L1 index 64
    uint32_t flags = PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D;
    l1[64] = (((phys_addr & ~0x3FFFFF) >> 12) << 10) | flags;

    __asm__ volatile("sfence.vma" ::: "memory");
    return (void*)phys_addr;
  }

  // Ramdisk Mapping (0x88000000)
  if (phys_addr == 0x88000000) {
    uint32_t virt_base = 0x20000000;
    uint32_t phys_base = 0x88000000;
    uint32_t num_blocks = 9; // 36MB (9 * 4MB blocks)

    uint32_t root_phys = (current_thread->pg_dir_phys & ((1U << 22) - 1)) << 12;
    uint32_t* l1 = (uint32_t*)root_phys;

    uint32_t start_l1_idx = (virt_base >> 22) & 0x3FF; // should be 128
    uint32_t flags = PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D;

    uint32_t i;
    for (i = 0; i < num_blocks; i++) {
      uint32_t block_phys = phys_base + i * 0x400000;
      l1[start_l1_idx + i] = (((block_phys & ~0x3FFFFF) >> 12) << 10) | flags;
    }

    __asm__ volatile("sfence.vma" ::: "memory");
    return (void*)virt_base;
  }

  return NULL;
}

void* thread_map_fb(void) { return NULL; }
