#include <riscv64/task.h>
#include <riscv64/interrupt.h>
#include <elf.h>
#include <clib.h>
#include <types.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/spinlock.h>

extern void ns16550_puts(const char* s);
extern void print_hex(uint64_t val);
extern void thread_entry_wrapper(void);
extern void userspace_entry_wrapper(void);
extern void thread_exit(void);

static Thread threads[MAX_THREADS];
static uint8_t thread_stacks[MAX_THREADS][STACK_SIZE] __attribute__((aligned(16)));
#include <kernel/cpu.h>
#define current_thread (get_cpu_local()->current_thread)
Spinlock sched_lock;
static CpuContext boot_contexts[MAX_CPUS];

extern uint64_t boot_pg_dir[];

#define USER_STACK_SIZE 0x20000 // 128KB


void thread_init(void) {
  int i;
  spin_init(&sched_lock);
  for (i = 0; i < MAX_THREADS; i++) {
    threads[i].state = THREAD_STATE_FREE;
    threads[i].tid = i;
    threads[i].stack_base = thread_stacks[i];
    threads[i].stack_size = STACK_SIZE;
    threads[i].cpu = -1;
    
    // Default kernel thread satp: Mode=Sv39, PPN of boot_pg_dir
    threads[i].pg_dir_phys = (8ULL << 60) | ((uint64_t)boot_pg_dir >> 12);
  }

  // Thread 0 represents the main boot thread
  threads[0].state = THREAD_STATE_RUNNING;
  threads[0].cpu = get_cpu_id();
  current_thread = &threads[0];

  ns16550_puts("Threads initialized. Main thread tid=0.\n");
}

int thread_create(void (*entry)(void)) {
  int i;
  IntDisable();
  spin_lock(&sched_lock);
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
      t->cpu = -1;

      ns16550_puts("Created thread tid=");
      print_hex(t->tid);
      ns16550_puts(" entry=");
      print_hex((uint64_t)entry);
      ns16550_puts("\n");

      spin_unlock(&sched_lock);
      IntEnable();
      return t->tid;
    }
  }
  spin_unlock(&sched_lock);
  IntEnable();
  return -1;
}

void thread_exit(void) {
  // TODO: Disable interrupts
  
  uint64_t boot_satp = (8ULL << 60) | ((uint64_t)boot_pg_dir >> 12);
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
  int curr_idx = current_thread ? current_thread->tid : 0;
  int next_idx;
  uint64_t hartid = get_cpu_id();

  IntDisable();
  spin_lock(&sched_lock);

retry:
  next_idx = -1;
  for (i = 1; i <= MAX_THREADS; i++) {
    int idx = (curr_idx + i) % MAX_THREADS;
    if (threads[idx].state == THREAD_STATE_READY && 
        (threads[idx].cpu == -1 || (current_thread && idx == current_thread->tid))) {
      next_idx = idx;
      break;
    }
  }

  if (next_idx != -1) {
    Thread* prev = current_thread;
    Thread* next = &threads[next_idx];

    if (prev == next) {
      next->state = THREAD_STATE_RUNNING;
      spin_unlock(&sched_lock);
      IntEnable();
      return;
    }

    if (prev) {
      if (prev->state == THREAD_STATE_RUNNING) {
        prev->state = THREAD_STATE_READY;
      }
    }
    next->state = THREAD_STATE_RUNNING;
    next->cpu = hartid;
    current_thread = next;

    // Update CPU-local kernel stack top for trap entry
    cpus[hartid].kernel_stack = (uint64_t)next->stack_base + next->stack_size;

    // Switch page tables (write to satp)
    uint64_t next_satp = next->pg_dir_phys;
    __asm__ volatile(
        "csrw satp, %0\n"
        "sfence.vma\n" ::"r"(next_satp)
        : "memory");

    CpuContext* prev_ctx = prev ? &prev->context : &boot_contexts[hartid];
    
    cpus[hartid].last_prev = prev;
    
    cpu_switch_to(prev_ctx, &next->context);

    // Resuming thread returns here (with sched_lock held)
    Thread* lp = cpus[hartid].last_prev;
    if (lp) {
        lp->cpu = -1;
    }

    // Resuming thread releases lock here
    spin_unlock(&sched_lock);
  } else {
    if (current_thread && current_thread->state == THREAD_STATE_RUNNING) {
      spin_unlock(&sched_lock);
      IntEnable();
      return;
    }
    
    // Idle loop
    spin_unlock(&sched_lock);
    IntEnable();
    __asm__ volatile("wfi");
    IntDisable();
    spin_lock(&sched_lock);
    goto retry;
  }
  IntEnable();
}

int thread_create_userspace(const unsigned char* bin, uint32_t size, const char* arg) {
  int i = 0;
  int ret_tid = -1;
  IntDisable();
  spin_lock(&sched_lock);
  for (i = 1; i < MAX_THREADS; i++) {
    if (threads[i].state == THREAD_STATE_FREE) {
      Thread* t = &threads[i];

      // Initialize Sv39 page tables via VMM
      t->pg_dir_phys = vmm_create_aspace();
      if (t->pg_dir_phys == 0) {
        ns16550_puts("Failed to create address space for thread!\n");
        goto cleanup;
      }

      uint64_t entry_point = 0x00100000;
      Elf64_Ehdr* ehdr = (Elf64_Ehdr*)bin;
      
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

              uint64_t seg_flags = VMM_FLAG_USER;
              if (phdr[j].p_flags & PF_R) seg_flags |= VMM_FLAG_READ;
              if (phdr[j].p_flags & PF_W) seg_flags |= VMM_FLAG_WRITE;
              if (phdr[j].p_flags & PF_X) seg_flags |= VMM_FLAG_EXEC;

              uint64_t start_page = vaddr & ~0xFFF;
              uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
              uint64_t curr_page;

              for (curr_page = start_page; curr_page < end_page; curr_page += 4096) {
                uint64_t phys_offset = curr_page - start_page;
                
                void* code_page = pmm_alloc_page();
                if (!code_page) goto cleanup;
                
                vmm_map(t->pg_dir_phys, curr_page, (uint64_t)code_page, seg_flags);
                
                if (phys_offset < filesz) {
                  uint64_t copy_size = filesz - phys_offset;
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
          uint64_t code_flags = VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_EXEC | VMM_FLAG_USER;
          for (p = 0; p < num_pages; p++) {
            uint64_t vaddr = 0x00100000 + p * 4096;
            void* code_page = pmm_alloc_page();
            if (!code_page) goto cleanup;
            
            vmm_map(t->pg_dir_phys, vaddr, (uint64_t)code_page, code_flags);
            
            uint64_t copy_size = size - p * 4096;
            if (copy_size > 4096) copy_size = 4096;
            CbMemCpy(code_page, bin + p * 4096, copy_size);
          }
      }

      // Map User Stack
      uint64_t stack_flags = VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER;
      uint32_t page_idx;
      void* last_stack_page = NULL;
      for (page_idx = 0; page_idx < (USER_STACK_SIZE / 4096); page_idx++) {
          void* stack_page = pmm_alloc_page();
          if (!stack_page) goto cleanup;
          
          uint64_t va = 0x00800000 - USER_STACK_SIZE + (page_idx * 4096);
          vmm_map(t->pg_dir_phys, va, (uint64_t)stack_page, stack_flags);
          
          if (page_idx == (USER_STACK_SIZE / 4096) - 1) {
              last_stack_page = stack_page;
          }
      }

      uint8_t* stk_top = (uint8_t*)t->stack_base + t->stack_size;
      stk_top -= sizeof(CpuContext);
      CpuContext* ctx = (CpuContext*)stk_top;

      CbMemSet((int8_t*)ctx, 0, sizeof(CpuContext));

      ctx->s1 = entry_point;
      ctx->ra = (uint64_t)userspace_entry_wrapper;
      ctx->sp = (uint64_t)stk_top;
      ctx->s0 = (uint64_t)stk_top;
      
      uint64_t user_sp = 0x00800000 - 0x100;
      ctx->s2 = user_sp;
      ctx->s3 = (uint64_t)t->stack_base + t->stack_size;

      // Setup argc, argv stubs on user stack
      uint64_t* ustack = (uint64_t*)((uint64_t)last_stack_page + 4096 - 0x100);
      
      if (arg) {
        ustack[0] = 2;                           // argc
        ustack[1] = user_sp + 16;                // argv
        ustack[2] = user_sp + 40;                // argv[0] -> "loader"
        ustack[3] = user_sp + 48;                // argv[1] -> arg
        ustack[4] = 0;                           // NULL
        CbMemCpy((char*)(ustack + 5), "loader\0", 7);
        int arg_len = CbStrLen((const int8_t*)arg);
        if (arg_len > 100) arg_len = 100;
        CbMemCpy((char*)(ustack + 6), arg, arg_len);
        ((char*)(ustack + 6))[arg_len] = '\0';
      } else {
        ustack[0] = 1;                           // argc
        ustack[1] = user_sp + 16;                // argv
        ustack[2] = user_sp + 32;                // argv[0] -> "shell"
        ustack[3] = 0;                           // NULL
        CbMemCpy((char*)(ustack + 4), "shell\0", 6);
      }

      t->context = *ctx;
      t->cpu = -1;
      t->state = THREAD_STATE_READY;

      ns16550_puts("Created userspace thread tid=");
      print_hex(t->tid);
      ns16550_puts(" entry=");
      print_hex(entry_point);
      ns16550_puts(" stack_base=");
      print_hex((uint64_t)t->stack_base);
      ns16550_puts(" stack_top=");
      print_hex(ctx->s3);
      ns16550_puts("\n");

      ret_tid = t->tid;
      break;
    }
  }

cleanup:
  if (ret_tid == -1 && i < MAX_THREADS) {
      if (threads[i].pg_dir_phys != 0) {
          vmm_free_aspace(threads[i].pg_dir_phys);
          threads[i].pg_dir_phys = 0;
      }
  }
  spin_unlock(&sched_lock);
  IntEnable();
  return ret_tid;
}

uint32_t thread_get_current_tid(void) {
  return current_thread->tid;
}

void thread_set_current_regs(RISCV64Registers* regs) {
  current_thread->regs = regs;
}

uint64_t translate_user_va(Thread* t, uint64_t va) {
  uint64_t root_phys = (t->pg_dir_phys & ((1ULL << 44) - 1)) << 12;
  uint64_t* l2 = (uint64_t*)root_phys;

  uint32_t l2_idx = (va >> 30) & 0x1FF;
  if (l2_idx != 0) return 0; // userspace must be in index 0 (0-1GB)

  uint64_t l2_entry = l2[l2_idx];
  if ((l2_entry & PTE_V) == 0) return 0;

  // SV39 allows leaf at Level 2 (1GB block), but we do not use it for user code.
  if ((l2_entry & (PTE_R | PTE_W | PTE_X)) != 0) {
    uint64_t ppn = (l2_entry >> 10) & ((1ULL << 44) - 1);
    return (ppn << 12) | (va & 0x3FFFFFFF);
  }

  uint64_t* l1 = (uint64_t*)(((l2_entry >> 10) & ((1ULL << 44) - 1)) << 12);
  uint32_t l1_idx = (va >> 21) & 0x1FF;
  uint64_t l1_entry = l1[l1_idx];
  if ((l1_entry & PTE_V) == 0) return 0;

  // Leaf at Level 1 (2MB block)
  if ((l1_entry & (PTE_R | PTE_W | PTE_X)) != 0) {
    uint64_t ppn = (l1_entry >> 10) & ((1ULL << 44) - 1);
    return (ppn << 12) | (va & 0x1FFFFF);
  }

  uint64_t* l0 = (uint64_t*)(((l1_entry >> 10) & ((1ULL << 44) - 1)) << 12);
  uint32_t l0_idx = (va >> 12) & 0x1FF;
  uint64_t l0_entry = l0[l0_idx];
  if ((l0_entry & PTE_V) == 0) return 0;

  uint64_t ppn = (l0_entry >> 10) & ((1ULL << 44) - 1);
  return (ppn << 12) | (va & 0xFFF);
}

int thread_ipc_send(uint32_t dest, void* buf, uint32_t size) {
  IntDisable();
  spin_lock(&sched_lock);

  if (dest == current_thread->tid || dest >= MAX_THREADS ||
      threads[dest].state == THREAD_STATE_FREE) {
    spin_unlock(&sched_lock);
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

    uint64_t src_phys = translate_user_va(current_thread, (uint64_t)buf);
    uint64_t dest_phys =
        translate_user_va(dest_thread, (uint64_t)dest_thread->ipc_buf);

    if (src_phys == 0 || dest_phys == 0) {
      ns16550_puts("IPC send: Translation failed. src_phys=");
      print_hex(src_phys);
      ns16550_puts(" dest_phys=");
      print_hex(dest_phys);
      ns16550_puts("\n");
      spin_unlock(&sched_lock);
      IntEnable();
      return -1;
    }

    CbMemCpy((void*)dest_phys, (void*)src_phys, copy_size);

    ns16550_puts("IPC send wake dest=");
    print_hex(dest_thread->tid);
    ns16550_puts(" regs=");
    print_hex((uint64_t)dest_thread->regs);
    ns16550_puts(" old_a0=");
    print_hex(dest_thread->regs->gpr[10]);

    dest_thread->regs->gpr[10] = copy_size; // a0 (x10) is return value

    ns16550_puts(" new_a0=");
    print_hex(dest_thread->regs->gpr[10]);
    ns16550_puts("\n");

    dest_thread->state = THREAD_STATE_READY;

    spin_unlock(&sched_lock);
    IntEnable();
    return 0;
  }

  current_thread->state = THREAD_STATE_BLOCKED;
  current_thread->ipc_partner = dest;
  current_thread->ipc_buf = buf;
  current_thread->ipc_size = size;

  spin_unlock(&sched_lock);
  schedule();

  return IPC_BLOCKED;
}

int thread_ipc_recv(uint32_t src, void* buf, uint32_t size) {
  IntDisable();
  spin_lock(&sched_lock);

  if (src != ANY_THREAD) {
    if (src == current_thread->tid || src >= MAX_THREADS ||
        threads[src].state == THREAD_STATE_FREE) {
      spin_unlock(&sched_lock);
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

    uint64_t src_phys =
        translate_user_va(sender_thread, (uint64_t)sender_thread->ipc_buf);
    uint64_t dest_phys = translate_user_va(current_thread, (uint64_t)buf);

    if (src_phys == 0 || dest_phys == 0) {
      ns16550_puts("IPC recv: Translation failed\n");
      spin_unlock(&sched_lock);
      IntEnable();
      return -1;
    }

    CbMemCpy((void*)dest_phys, (void*)src_phys, copy_size);

    sender_thread->regs->gpr[10] = 0; // a0 (x10) is return value for sender
    sender_thread->state = THREAD_STATE_READY;

    spin_unlock(&sched_lock);
    IntEnable();
    return copy_size;
  }

  current_thread->state = THREAD_STATE_BLOCKED;
  current_thread->ipc_partner = src;
  current_thread->ipc_buf = buf;
  current_thread->ipc_size = size;

  spin_unlock(&sched_lock);
  schedule();

  return IPC_BLOCKED;
}

void* thread_map_mmio(uint64_t phys_addr) {
  // QEMU Virt NS16550 UART (0x10000000)
  if (phys_addr == 0x10000000) {
    uint64_t flags = VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER;
    vmm_map(current_thread->pg_dir_phys, 0x10000000, 0x10000000, flags);

    __asm__ volatile("sfence.vma" ::: "memory");

    ns16550_puts("Mapped UART MMIO ");
    print_hex(phys_addr);
    ns16550_puts(" for U-mode access in tid ");
    print_hex(current_thread->tid);
    ns16550_puts("\n");

    return (void*)phys_addr;
  }

  // Ramdisk Mapping (0x88000000)
  if (phys_addr == 0x88000000) {
    uint64_t virt_base = 0x20000000;
    uint64_t phys_base = 0x88000000;
    uint64_t size = 34 * 1024 * 1024; // 34MB
    uint64_t flags = VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER;

    uint64_t offset;
    for (offset = 0; offset < size; offset += 4096) {
      if (vmm_map(current_thread->pg_dir_phys, virt_base + offset, phys_base + offset, flags) != 0) {
        ns16550_puts("Failed to map ramdisk page at offset ");
        print_hex(offset);
        ns16550_puts("\n");
        return NULL;
      }
    }

    __asm__ volatile("sfence.vma" ::: "memory");

    ns16550_puts("Mapped Ramdisk RAM ");
    print_hex(phys_addr);
    ns16550_puts(" to virt ");
    print_hex(virt_base);
    ns16550_puts(" in tid ");
    print_hex(current_thread->tid);
    ns16550_puts("\n");

    return (void*)virt_base;
  }

  return NULL;
}

void* thread_map_fb(void) { return NULL; }

uint64_t sys_mmap_impl(uint64_t addr, uint64_t size) {
  static uint64_t next_vaddr = 0x20000000; // 512MB mark

  if (size == 0) return next_vaddr;

  uint64_t ret_vaddr = addr ? addr : next_vaddr;
  uint64_t num_blocks = (size + 0x1FFFFF) >> 21; // Number of 2MB blocks

  uint64_t num_pages = (size + 4095) / 4096;
  uint64_t i;
  for (i = 0; i < num_pages; i++) {
    void* phys_page = pmm_alloc_page();
    if (!phys_page) {
        ns16550_puts("sys_mmap: pmm_alloc_page failed!\n");
        return 0;
    }
    if (vmm_map(current_thread->pg_dir_phys, ret_vaddr + i * 4096, (uint64_t)phys_page, VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER) != 0) {
        ns16550_puts("sys_mmap: vmm_map failed!\n");
        return 0;
    }
  }

  if (ret_vaddr == next_vaddr) {
    next_vaddr += num_blocks * 0x200000;
  }

  __asm__ volatile("sfence.vma" ::: "memory");

  CbMemSet((int8_t*)ret_vaddr, 0, num_pages * 4096);

  ns16550_puts("sys_mmap size=");
  print_hex(size);
  ns16550_puts(" vaddr=");
  print_hex(ret_vaddr);
  ns16550_puts("\n");

  return ret_vaddr;
}

