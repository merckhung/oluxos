#include <riscv32/task.h>
#include <riscv32/interrupt.h>
#include <elf.h>
#include <clib.h>
#include <types.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>

extern void ns16550_puts(const char* s);
extern void print_hex(uint64_t val);
extern void thread_entry_wrapper(void);
extern void userspace_entry_wrapper(void);
extern void thread_exit(void);

static Thread threads[MAX_THREADS];
static uint8_t thread_stacks[MAX_THREADS][STACK_SIZE] __attribute__((aligned(16)));
static Thread* current_thread = NULL;

extern uint32_t boot_pg_dir[];

#define USER_STACK_SIZE 0x20000 // 128KB


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

  uint32_t boot_satp = (1UL << 31) | ((uint32_t)boot_pg_dir >> 12);
  if (current_thread->pg_dir_phys != 0 && current_thread->pg_dir_phys != boot_satp) {
      vmm_free_aspace(current_thread->pg_dir_phys);
      current_thread->pg_dir_phys = 0;
  }

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


int thread_create_userspace(const unsigned char* bin, uint32_t size) {
  int i;
  IntDisable();
  for (i = 1; i < MAX_THREADS; i++) {
    if (threads[i].state == THREAD_STATE_FREE) {
      Thread* t = &threads[i];

      // Initialize Sv32 page tables via VMM
      t->pg_dir_phys = vmm_create_aspace();
      if (t->pg_dir_phys == 0) {
        ns16550_puts("Failed to create address space for thread!\n");
        IntEnable();
        return -1;
      }

      // Default flags for raw fallback (RWX)
      uint32_t code_flags = VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_EXEC | VMM_FLAG_USER;
      
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

              uint32_t seg_flags = VMM_FLAG_USER;
              if (phdr[j].p_flags & PF_R) seg_flags |= VMM_FLAG_READ;
              if (phdr[j].p_flags & PF_W) seg_flags |= VMM_FLAG_WRITE;
              if (phdr[j].p_flags & PF_X) seg_flags |= VMM_FLAG_EXEC;

              uint32_t start_page = vaddr & ~0xFFF;
              uint32_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
              uint32_t curr_page;

              for (curr_page = start_page; curr_page < end_page; curr_page += 4096) {
                uint32_t phys_offset = curr_page - start_page;
                
                void* code_page = pmm_alloc_page();
                if (!code_page) {
                    ns16550_puts("Failed to allocate code page!\n");
                    IntEnable();
                    return -1;
                }
                
                vmm_map(t->pg_dir_phys, curr_page, (uint32_t)code_page, seg_flags);
                
                if (phys_offset < filesz) {
                  uint32_t copy_size = filesz - phys_offset;
                  if (copy_size > 4096) copy_size = 4096;
                  CbMemCpy(code_page, bin + offset + phys_offset, copy_size);
                }
              }
            }
          }
      } else {
          // Raw binary
          uint32_t num_pages = (size + 4095) / 4096;
          uint32_t p;
          for (p = 0; p < num_pages; p++) {
            uint32_t vaddr = 0x00100000 + p * 4096;
            void* code_page = pmm_alloc_page();
            if (!code_page) {
                IntEnable();
                return -1;
            }
            vmm_map(t->pg_dir_phys, vaddr, (uint32_t)code_page, code_flags);
            
            uint32_t copy_size = size - p * 4096;
            if (copy_size > 4096) copy_size = 4096;
            CbMemCpy(code_page, bin + p * 4096, copy_size);
          }
      }

      // Map User Stack
      uint32_t stack_flags = VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER;
      uint32_t page_idx;
      void* last_stack_page = NULL;
      for (page_idx = 0; page_idx < (USER_STACK_SIZE / 4096); page_idx++) {
          void* stack_page = pmm_alloc_page();
          if (!stack_page) {
              IntEnable();
              return -1;
          }
          uint32_t va = 0x00800000 - USER_STACK_SIZE + (page_idx * 4096);
          vmm_map(t->pg_dir_phys, va, (uint32_t)stack_page, stack_flags);
          
          if (page_idx == (USER_STACK_SIZE / 4096) - 1) {
              last_stack_page = stack_page;
          }
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
      ctx->s3 = (uint32_t)t->stack_base + t->stack_size; // Kernel Stack Top

      // Setup argc, argv stubs on user stack
      uint32_t* ustack = (uint32_t*)((uint32_t)last_stack_page + 4096 - 0x100);
      ustack[0] = 1;                            // argc
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
    if (vmm_map(current_thread->pg_dir_phys, 0x10000000, 0x10000000, VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER) != 0) {
        return NULL;
    }
    return (void*)phys_addr;
  }

  // Ramdisk Mapping (0x88000000)
  if (phys_addr == 0x88000000) {
    uint32_t virt_base = 0x20000000;
    uint32_t phys_base = 0x88000000;
    uint32_t size = 36 * 1024 * 1024; // 36MB

    uint32_t offset;
    for (offset = 0; offset < size; offset += 4096) {
      if (vmm_map(current_thread->pg_dir_phys, virt_base + offset, phys_base + offset, VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER) != 0) {
        ns16550_puts("Failed to map ramdisk page at offset ");
        print_hex(offset);
        ns16550_puts("\n");
        return NULL;
      }
    }

    __asm__ volatile("sfence.vma" ::: "memory");
    return (void*)virt_base;
  }

  return NULL;
}

void* thread_map_fb(void) { return NULL; }
