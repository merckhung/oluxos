#include <arm64/interrupt.h>
#include <arm64/task.h>
#include <clib.h>
#include <types.h>
#include <driver/fb.h>
#include <elf.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>

void pl011_puts(const char* s);
void pl011_putc(char c);
void print_hex(uint64_t val);
void thread_entry_wrapper(void);  // Assembly wrapper
uint64_t translate_user_va(Thread* t, uint64_t va);

Thread threads[MAX_THREADS];
static uint8_t thread_stacks[MAX_THREADS][STACK_SIZE]
    __attribute__((aligned(16)));
Thread* current_thread = NULL;

extern volatile uint64_t pg_dir[512];



void thread_init(void) {
  int i;
  for (i = 0; i < MAX_THREADS; i++) {
    threads[i].state = THREAD_STATE_FREE;
    threads[i].tid = i;
    threads[i].stack_base = thread_stacks[i];
    threads[i].stack_size = STACK_SIZE;
    threads[i].pg_dir_phys = (uint64_t)pg_dir;  // Default to boot page table
    int j;
    for (j = 0; j < MAX_KERNEL_FDS; j++) {
      threads[i].fds[j].used = 0;
    }
  }

  // Thread 0 represents the main boot thread
  threads[0].state = THREAD_STATE_RUNNING;
  threads[0].pg_dir_phys = (uint64_t)pg_dir;
  current_thread = &threads[0];

  pl011_puts("Threads initialized. Main thread tid=0.\n");
}

int thread_create(void (*entry)(void)) {
  int i;
  IntDisable();
  for (i = 1; i < MAX_THREADS; i++) {
    if (threads[i].state == THREAD_STATE_FREE) {
      Thread* t = &threads[i];

      t->pg_dir_phys = (uint64_t)pg_dir;  // Kernel threads use boot page table

      // Setup fake context on stack
      uint8_t* stk_top = (uint8_t*)t->stack_base + t->stack_size;

      stk_top -= sizeof(CpuContext);
      CpuContext* ctx = (CpuContext*)stk_top;

      CbMemSet((int8_t*)ctx, 0, sizeof(CpuContext));

      ctx->x19 = (uint64_t)entry;
      ctx->lr = (uint64_t)thread_entry_wrapper;
      ctx->sp = (uint64_t)stk_top;
      ctx->fp = (uint64_t)stk_top;

      t->context = *ctx;
      t->state = THREAD_STATE_READY;

      pl011_puts("Created thread tid=");
      print_hex(t->tid);
      pl011_puts(" entry=");
      print_hex((uint64_t)entry);
      pl011_puts("\n");

      IntEnable();
      return t->tid;
    }
  }
  IntEnable();
  return -1;
}

void thread_exit(void) {
  IntDisable();
  pl011_puts("thread_exit: tid="); print_hex(current_thread->tid);
  pl011_puts(" clear_child_tid="); print_hex(current_thread->clear_child_tid);
  pl011_puts(" pgdir="); print_hex(current_thread->pg_dir_phys);
  pl011_puts("\n");

  if (current_thread->clear_child_tid) {
    uint64_t pa = translate_user_va(current_thread, current_thread->clear_child_tid);
    pl011_puts("  clear_child_tid pa="); print_hex(pa); pl011_puts("\n");
    if (pa != 0) {
      *(int*)pa = 0;
    }
  }

  if (current_thread->pg_dir_phys != 0 && current_thread->pg_dir_phys != (uint64_t)pg_dir) {
      vmm_free_aspace(current_thread->pg_dir_phys);
      current_thread->pg_dir_phys = 0;
  }

  current_thread->state = THREAD_STATE_FREE;
  pl011_puts("Thread ");
  print_hex(current_thread->tid);
  pl011_puts(" exited.\n");
  schedule();
  while (1);
}

void schedule(void) {
  int i;
  int curr_idx = current_thread->tid;
  int next_idx;

  IntDisable();

// pl011_puts("SCHED: curr="); print_hex(curr_idx); pl011_puts("\n");

retry:
  next_idx = -1;
  for (i = 1; i <= MAX_THREADS; i++) {
    int idx = (curr_idx + i) % MAX_THREADS;
    if (idx != 0 && threads[idx].state == THREAD_STATE_READY) {
      next_idx = idx;
      break;
    }
  }

  if (next_idx != -1) {
    // pl011_puts("SCHED: pick="); print_hex(next_idx); pl011_puts("\n");
    Thread* prev = current_thread;
    Thread* next = &threads[next_idx];

    if (prev->state == THREAD_STATE_RUNNING) {
      prev->state = THREAD_STATE_READY;
    }
    next->state = THREAD_STATE_RUNNING;
    current_thread = next;

    // Switch page tables
    uint64_t next_pg_dir = next->pg_dir_phys;
    // pl011_puts("SCHED: switch pgdir to "); print_hex(next_pg_dir); pl011_puts("\n");
    __asm__ volatile(
        "msr ttbr0_el1, %0\n"
        "tlbi vmalle1is\n"
        "dsb sy\n"
        "isb\n" ::"r"(next_pg_dir)
        : "memory");

    // pl011_puts("SCHED: cpu_switch_to "); print_hex(prev->tid); pl011_puts(" -> "); print_hex(next->tid); pl011_puts("\n");
    cpu_switch_to(&prev->context, &next->context);
    // pl011_puts("SCHED: returned to "); print_hex(current_thread->tid); pl011_puts("\n");
  } else {
    if (current_thread->state == THREAD_STATE_RUNNING) {
      return;
    }
    uint64_t current_ttbr0;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(current_ttbr0));
    if (current_ttbr0 != (uint64_t)pg_dir) {
      __asm__ volatile(
          "msr ttbr0_el1, %0\n"
          "tlbi vmalle1is\n"
          "dsb sy\n"
          "isb\n" ::"r"((uint64_t)pg_dir)
          : "memory");
    }
    /*
    pl011_puts("SCHED: idle\n");
    for (i = 1; i < MAX_THREADS; i++) {
        pl011_puts("  tid="); print_hex(i); pl011_puts(" state="); print_hex(threads[i].state); pl011_puts(" partner="); print_hex(threads[i].ipc_partner); pl011_puts("\n");
    }
    */
    // Idle loop
    IntEnable();
    __asm__ volatile("wfi");
    IntDisable();
    goto retry;
  }
}

#define USER_STACK_SIZE 65536
extern void userspace_entry_wrapper(void);

int thread_create_userspace(const unsigned char* bin, uint32_t size, const char* arg) {
  int i;
  IntDisable();
  
  pl011_puts("sys_spawn size: ");
  print_hex(size);
  pl011_puts("\n");
  
  for (i = 1; i < MAX_THREADS; i++) {
    if (threads[i].state == THREAD_STATE_FREE) {
      Thread* t = &threads[i];
      int j;
      for (j = 0; j < MAX_KERNEL_FDS; j++) {
        t->fds[j].used = 0;
      }
      CbMemCpy(t->cwd, "/", 2);

      // Initialize private address space
      t->pg_dir_phys = vmm_create_aspace();
      if (t->pg_dir_phys == 0) {
        pl011_puts("Failed to create address space!\n");
        IntEnable();
        return -1;
      }

      uint64_t code_flags = VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_EXEC | VMM_FLAG_USER;
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

              uint64_t seg_flags = VMM_FLAG_USER;
              if (phdr[j].p_flags & PF_R) seg_flags |= VMM_FLAG_READ;
              if (phdr[j].p_flags & PF_W) seg_flags |= VMM_FLAG_WRITE;
              if (phdr[j].p_flags & PF_X) seg_flags |= VMM_FLAG_EXEC;

              uint64_t start_page = vaddr & ~0xFFF;
              uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
              uint64_t curr_page;
              
              pl011_puts("LOAD Segment: vaddr="); print_hex(vaddr);
              pl011_puts(" offset="); print_hex(offset);
              pl011_puts(" filesz="); print_hex(filesz);
              pl011_puts("\n");
              
              for (curr_page = start_page; curr_page < end_page; curr_page += 4096) {
                  uint64_t phys_offset = curr_page - start_page;
                  void* code_page = pmm_alloc_page();
                  if (!code_page) {
                      IntEnable();
                      return -1;
                  }
                  vmm_map(t->pg_dir_phys, curr_page, (uint64_t)code_page, seg_flags);
                  
                  if (phys_offset < filesz) {
                      uint64_t copy_size = filesz - phys_offset;
                      if (copy_size > 4096) copy_size = 4096;
                      CbMemCpy(code_page, bin + offset + phys_offset, copy_size);
                  }
              }
            }
          }
          
          // Map userspace Heap (0x509000 to 0x600000)
          uint64_t heap_start = 0x509000;
          t->brk = heap_start;
          uint64_t heap_end = 0x600000;
          uint64_t heap_curr;
          for (heap_curr = heap_start; heap_curr < heap_end; heap_curr += 4096) {
               void* heap_page = pmm_alloc_page();
               if (!heap_page) {
                   IntEnable();
                   return -1;
               }
               CbMemSet(heap_page, 0, 4096);
               vmm_map(t->pg_dir_phys, heap_curr, (uint64_t)heap_page, code_flags);
          }
      } else {
          // Raw binary
          uint32_t num_pages = (size + 4095) / 4096;
          t->brk = 0x100000 + num_pages * 4096;
          uint32_t p;
          for (p = 0; p < num_pages; p++) {
            uint64_t vaddr = 0x00100000 + p * 4096;
            void* code_page = pmm_alloc_page();
            if (!code_page) {
                IntEnable();
                return -1;
            }
            vmm_map(t->pg_dir_phys, vaddr, (uint64_t)code_page, code_flags);
            
            uint64_t copy_size = size - p * 4096;
            if (copy_size > 4096) copy_size = 4096;
            CbMemCpy(code_page, bin + p * 4096, copy_size);
          }
      }

      // Map User Stack
      uint64_t stack_flags = VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER;
      int page_idx;
      void* last_stack_page = NULL;
      for (page_idx = 0; page_idx < (USER_STACK_SIZE / 4096); page_idx++) {
          void* stack_page = pmm_alloc_page();
          if (!stack_page) {
              IntEnable();
              return -1;
          }
          CbMemSet(stack_page, 0, 4096);
          uint64_t va = 0x00800000 - USER_STACK_SIZE + (page_idx * 4096);
          vmm_map(t->pg_dir_phys, va, (uint64_t)stack_page, stack_flags);
          if (page_idx == (USER_STACK_SIZE / 4096) - 1) {
              last_stack_page = stack_page;
          }
      }
      extern void vmm_dump_path(uint64_t aspace, uint64_t va);
      vmm_dump_path(t->pg_dir_phys, 0x7FFF00);


      uint8_t* stk_top = (uint8_t*)t->stack_base + t->stack_size;
      stk_top -= sizeof(CpuContext);
      CpuContext* ctx = (CpuContext*)stk_top;

      CbMemSet((int8_t*)ctx, 0, sizeof(CpuContext));

      ctx->x19 = entry_point;
      ctx->lr = (uint64_t)userspace_entry_wrapper;
      ctx->sp = (uint64_t)stk_top;
      ctx->fp = (uint64_t)stk_top;

      uint64_t user_sp = 0x00800000 - 0x100;
      ctx->x20 = user_sp; // Passed to switch.S for SP_EL0
      
      uint64_t* ustack = (uint64_t*)((uint64_t)last_stack_page + 4096 - 0x100);
      
      if (arg) {
        // Dynamic spawn with 1 argument (e.g. loader "/bin/busybox")
        ustack[0] = 2;                           // argc
        ustack[1] = user_sp + 0xD0;              // argv[0] -> "loader"
        ustack[2] = user_sp + 0xD8;              // argv[1] -> arg
        ustack[3] = 0;                           // argv[2] (NULL)
        ustack[4] = 0;                           // envp[0] (NULL)
        
        // Setup simple auxv
        ustack[6] = 6;                          // AT_PAGESZ
        ustack[7] = 4096;                       // 4096
        ustack[8] = 0;                          // AT_NULL
        ustack[9] = 0;
        
        // Copy strings
        CbMemCpy((int8_t*)&ustack[26], "loader\0", 7);
        int arg_len = CbStrLen((const int8_t*)arg);
        if (arg_len > 39) arg_len = 39; // limit to 39 bytes to fit in ustack[27..31]
        CbMemCpy((int8_t*)&ustack[27], arg, arg_len);
        ((char*)&ustack[27])[arg_len] = '\0';
        
        pl011_puts("KRN ustack="); print_hex((uint64_t)ustack);
        pl011_puts(" argc="); print_hex(ustack[0]);
        pl011_puts(" argv0="); print_hex(ustack[1]);
        pl011_puts(" argv1="); print_hex(ustack[2]);
        pl011_puts("\nKRN str0="); pl011_puts((const char*)&ustack[26]);
        pl011_puts(" str1="); pl011_puts((const char*)&ustack[27]); pl011_puts("\n");
      } else {
        // Default static spawn (for shell)
        ustack[0] = 3;                           // argc
        ustack[1] = user_sp + 0xD0;              // argv[0] -> "busybox"
        ustack[2] = user_sp + 0xD8;              // argv[1] -> "echo"
        ustack[3] = user_sp + 0xE0;              // argv[2] -> "Hello OluxOS!"
        ustack[4] = 0;                           // argv[3] (NULL)
        ustack[5] = 0;                           // envp[0] (NULL)
        
        ustack[6] = 3;                           // AT_PHDR
        ustack[7] = 0x400000 + ehdr->e_phoff;    // phdr vaddr
        ustack[8] = 4;                           // AT_PHENT
        ustack[9] = ehdr->e_phentsize;           // phent
        ustack[10] = 5;                           // AT_PHNUM
        ustack[11] = ehdr->e_phnum;               // phnum
        
        ustack[12] = 25;                         // AT_RANDOM
        ustack[13] = user_sp + 0xF0;             // pointer to random bytes
        ustack[14] = 6;                          // AT_PAGESZ
        ustack[15] = 4096;                       // 4096
        ustack[16] = 31;                         // AT_EXECFN
        ustack[17] = user_sp + 0xC0;             // pointer to "/bin/busybox"
        ustack[18] = 0;                          // AT_NULL
        ustack[19] = 0;                          // auxv val
        
        CbMemCpy((int8_t*)&ustack[24], "/bin/busybox\0", 13);
        CbMemCpy((int8_t*)&ustack[26], "busybox\0", 8);
        CbMemCpy((int8_t*)&ustack[27], "echo\0\0\0\0", 8);
        CbMemCpy((int8_t*)&ustack[28], "Hello OluxOS!\0\0", 16);
        ustack[30] = 0x123456789ABCDEF0;
        ustack[31] = 0x0FEDCBA987654321;
      }

      t->context = *ctx;
      t->state = THREAD_STATE_READY;

      pl011_puts("Created userspace thread tid=");
      print_hex(t->tid);
      pl011_puts("\n");

      IntEnable();
      return t->tid;
    }
  }
  IntEnable();
  return -1;
}

uint32_t thread_get_current_tid(void) { return current_thread->tid; }

void thread_set_current_regs(ARM64Registers* regs) {
  current_thread->regs = regs;
}

uint64_t translate_user_va(Thread* t, uint64_t va) {
  uint64_t* l1 = (uint64_t*)t->pg_dir_phys;

  uint32_t l1_idx = (va >> 30) & 0x1FF;
  uint64_t l1_entry = l1[l1_idx];
  if ((l1_entry & 0x1) == 0) return 0;  // Invalid

  if ((l1_entry & 0x2) == 0) {
    // 1GB Block
    uint64_t phys = l1_entry & 0x0000FFFFC0000000ULL;
    return phys | (va & 0x3FFFFFFF);
  }

  uint64_t* l2 = (uint64_t*)(l1_entry & ~0xFFF);
  uint32_t l2_idx = (va >> 21) & 0x1FF;
  uint64_t l2_entry = l2[l2_idx];
  if ((l2_entry & 0x1) == 0) return 0;  // Invalid

  if ((l2_entry & 0x2) == 0) {
    // 2MB Block
    uint64_t phys = l2_entry & 0x0000FFFFFFE00000ULL;
    return phys | (va & 0x1FFFFF);
  }

  uint64_t* l3 = (uint64_t*)(l2_entry & ~0xFFF);
  uint32_t l3_idx = (va >> 12) & 0x1FF;
  uint64_t l3_entry = l3[l3_idx];
  if ((l3_entry & 0x3) != 0x3) return 0;  // Invalid or not Page

  uint64_t phys = l3_entry & 0x0000FFFFFFFFF000ULL;
  return phys | (va & 0xFFF);
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

    uint64_t src_phys = translate_user_va(current_thread, (uint64_t)buf);
    uint64_t dest_phys =
        translate_user_va(dest_thread, (uint64_t)dest_thread->ipc_buf);

    if (src_phys == 0 || dest_phys == 0) {
      pl011_puts("IPC send: Translation failed. src_phys=");
      print_hex(src_phys);
      pl011_puts(" dest_phys=");
      print_hex(dest_phys);
      pl011_puts(" buf=");
      print_hex((uint64_t)buf);
      pl011_puts(" dest_thread->ipc_buf=");
      print_hex((uint64_t)dest_thread->ipc_buf);
      pl011_puts(" current_thread->tid=");
      print_hex(current_thread->tid);
      pl011_puts(" dest_thread->tid=");
      print_hex(dest_thread->tid);
      pl011_puts("\n");
      IntEnable();
      return -1;
    }

    CbMemCpy((void*)dest_phys, (void*)src_phys, copy_size);

    dest_thread->regs->x[0] = copy_size;
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

    uint64_t src_phys =
        translate_user_va(sender_thread, (uint64_t)sender_thread->ipc_buf);
    uint64_t dest_phys = translate_user_va(current_thread, (uint64_t)buf);

    if (src_phys == 0 || dest_phys == 0) {
      pl011_puts("IPC recv: Translation failed\n");
      IntEnable();
      return -1;
    }

    CbMemCpy((void*)dest_phys, (void*)src_phys, copy_size);

    sender_thread->regs->x[0] = 0;
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

void* thread_map_mmio(uint64_t phys_addr) {
#if CONFIG_KDBGER
#if CONFIG_BOARD_RPI4
  if (phys_addr == 0xFE201000) {
    pl011_puts("thread_map_mmio: UART mapping blocked (KDBGER active)\n");
    return NULL;
  }
#else
  if (phys_addr == 0x09000000) {
    pl011_puts("thread_map_mmio: UART mapping blocked (KDBGER active)\n");
    return NULL;
  }
#endif
#endif
#if CONFIG_BOARD_RPI4
  if (phys_addr == 0xFE201000 || phys_addr == 0xFF840000) {
    uint32_t l1_idx = (phys_addr >> 30) & 0x1FF; // should be 3
    uint32_t l2_idx = (phys_addr >> 21) & 0x1FF;
    uint64_t* l1 = (uint64_t*)current_thread->pg_dir_phys;
    uint64_t* l2 = (uint64_t*)(l1[l1_idx] & ~0xFFF);

    uint64_t entry = l2[l2_idx];
    if ((entry & 0x3) == 0) {
      pl011_puts("thread_map_mmio: Entry not present in L2\n");
      return NULL;
    }

    entry = (entry & ~(3ULL << 6)) | (1ULL << 6);
    l2[l2_idx] = entry;

    __asm__ volatile(
        "tlbi vmalle1is\n"
        "dsb sy\n"
        "isb\n" ::
            : "memory");

    pl011_puts("Mapped MMIO ");
    print_hex(phys_addr);
    pl011_puts(" for EL0 access in tid ");
    print_hex(current_thread->tid);
    pl011_puts("\n");

    return (void*)phys_addr;
  }
#else // QEMU virt (Default)
  if (phys_addr == 0x09000000 || phys_addr == 0x08000000) {
    uint32_t l2_idx = phys_addr >> 21;
    uint64_t* l1 = (uint64_t*)current_thread->pg_dir_phys;
    uint64_t* l2 = (uint64_t*)(l1[0] & ~0xFFF);

    uint64_t entry = l2[l2_idx];
    if ((entry & 0x3) == 0) {
      pl011_puts("thread_map_mmio: Entry not present in L2\n");
      return NULL;
    }

    entry = (entry & ~(3ULL << 6)) | (1ULL << 6);
    l2[l2_idx] = entry;

    __asm__ volatile(
        "tlbi vmalle1is\n"
        "dsb sy\n"
        "isb\n" ::
            : "memory");

    pl011_puts("Mapped MMIO ");
    print_hex(phys_addr);
    pl011_puts(" for EL0 access in tid ");
    print_hex(current_thread->tid);
    pl011_puts("\n");

    return (void*)phys_addr;
  }
#endif

  // Shared Ramdisk mapping (same for both!)
  if (phys_addr == 0x48000000) {
    uint64_t virt_base = 0x10000000;
    uint64_t phys_base = 0x48000000;
    uint32_t size = 34 * 1024 * 1024; // 34MB

    uint64_t offset;
    for (offset = 0; offset < size; offset += 4096) {
      if (vmm_map(current_thread->pg_dir_phys, virt_base + offset, phys_base + offset, VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER) != 0) {
        pl011_puts("Failed to map ramdisk page at offset ");
        print_hex(offset);
        pl011_puts("\n");
        return NULL;
      }
    }

    __asm__ volatile(
        "tlbi vmalle1is\n"
        "dsb sy\n"
        "isb\n" ::
            : "memory");

    pl011_puts("Mapped Ramdisk ");
    print_hex(phys_addr);
    pl011_puts(" to virtual ");
    print_hex(virt_base);
    pl011_puts(" for tid ");
    print_hex(current_thread->tid);
    pl011_puts("\n");

    return (void*)virt_base;
  }

  pl011_puts("thread_map_mmio: Unauthorized address: ");
  print_hex(phys_addr);
  pl011_puts("\n");
  return NULL;
}

void* thread_map_fb(void) {
  uint64_t phys_base = fb_phys_addr;
  uint64_t virt_base = 0x02000000;
  uint32_t num_pages = (FB_WIDTH * FB_HEIGHT * FB_BPP + 4095) / 4096;

  uint32_t i;
  for (i = 0; i < num_pages; i++) {
    uint64_t paddr = phys_base + i * 4096;
    uint64_t vaddr = virt_base + i * 4096;
    if (vmm_map(current_thread->pg_dir_phys, vaddr, paddr, VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER | VMM_FLAG_DEVICE) != 0) {
        pl011_puts("Failed to map FB page!\n");
        return NULL;
    }
  }

  __asm__ volatile(
      "tlbi vmalle1is\n"
      "dsb sy\n"
      "isb\n" ::
          : "memory");

  pl011_puts("Mapped FB to virtual ");
  print_hex(virt_base);
  pl011_puts(" for tid ");
  print_hex(current_thread->tid);
  pl011_puts("\n");

  return (void*)virt_base;
}

uint64_t sys_mmap_impl(uint64_t addr, uint64_t size, uint64_t prot) {
  // Always return 2MB aligned allocations
  static uint64_t next_vaddr = 0x20000000; // 512MB mark

  if (size == 0) return next_vaddr;

  uint64_t ret_vaddr = addr ? addr : next_vaddr;
  uint64_t num_blocks = (size + 0x1FFFFF) >> 21; // Number of 2MB blocks

  uint64_t num_pages = (size + 4095) / 4096;
  uint64_t i;
  for (i = 0; i < num_pages; i++) {
    void* phys_page = pmm_alloc_page();
    if (!phys_page) {
        pl011_puts("sys_mmap: pmm_alloc_page failed!\n");
        return 0;
    }
    if (vmm_map(current_thread->pg_dir_phys, ret_vaddr + i * 4096, (uint64_t)phys_page, prot | VMM_FLAG_USER) != 0) {
        pl011_puts("sys_mmap: vmm_map failed!\n");
        return 0;
    }
  }

  if (ret_vaddr == next_vaddr) {
    next_vaddr += num_blocks * 0x200000;
  }

  __asm__ volatile(
      "tlbi vmalle1is\n"
      "dsb sy\n"
      "isb\n" ::: "memory");

  CbMemSet((int8_t*)ret_vaddr, 0, num_pages * 4096);

  pl011_puts("sys_mmap size=");
  print_hex(size);
  pl011_puts(" vaddr=");
  print_hex(ret_vaddr);
  pl011_puts("\n");

  return ret_vaddr;
}

int thread_block_on_uart(void* buf, uint32_t len) {
  IntDisable();
  current_thread->state = THREAD_STATE_BLOCKED;
  current_thread->ipc_partner = UART_HARDWARE;
  current_thread->ipc_buf = buf;
  current_thread->ipc_size = len;
  schedule();

  // Resumed here! Interrupts are still disabled.
  // Now read from ring buffer!
  extern int rx_buf_pop(char* c);
  extern int copy_to_user(uint64_t user_dest, const void* src, int len);

  char c;
  int count_read = 0;
  while (count_read < len && rx_buf_pop(&c)) {
    if ((g_console_termios.c_iflag & 0x100) && c == '\r') { // ICRNL
      c = '\n';
    }
    if (copy_to_user((uint64_t)buf + count_read, &c, 1) < 0) {
      pl011_puts("thread_block_on_uart: copy_to_user failed!\n");
      break;
    }
    // Echo
    if (g_console_termios.c_lflag & 0x008) { // ECHO
      if (c == '\n') pl011_putc('\r');
      pl011_putc(c);
    }
    count_read++;
  }

  IntEnable();
  return count_read;
}

int thread_fork(ARM64Registers* regs, uint64_t flags, uint64_t newsp) {
  int i;
  IntDisable();
  for (i = 1; i < MAX_THREADS; i++) {
    if (threads[i].state == THREAD_STATE_FREE) {
      Thread* child = &threads[i];
      Thread* parent = current_thread;

      // Save child's private stack info before overwrite
      void* child_stack_base = child->stack_base;
      uint32_t child_stack_size = child->stack_size;

      // Copy thread struct (including FDs, CWD, etc.)
      CbMemCpy((int8_t*)child, (const int8_t*)parent, sizeof(Thread));
      child->tid = i; // Keep correct TID
      child->stack_base = child_stack_base;
      child->stack_size = child_stack_size;
      child->clear_child_tid = 0; // Reset for child
      child->ipc_partner = 0;
      child->ipc_buf = NULL;
      child->ipc_size = 0;

      // Duplicate address space
      child->pg_dir_phys = vmm_dup_aspace(parent->pg_dir_phys);
      if (child->pg_dir_phys == 0) {
        child->state = THREAD_STATE_FREE;
        IntEnable();
        return -1;
      }

      // Copy parent's registers to child's kernel stack
      uint8_t* child_stk_top = (uint8_t*)child->stack_base + child->stack_size;
      child_stk_top -= sizeof(ARM64Registers);
      ARM64Registers* child_regs = (ARM64Registers*)child_stk_top;

      // Copy parent's saved registers
      CbMemCpy((int8_t*)child_regs, (const int8_t*)regs, sizeof(ARM64Registers));

      // Child should return 0 from clone/fork
      child_regs->x[0] = 0;

      // If newsp (child_sp) is specified, use it. Otherwise use parent's SP_EL0.
      if (newsp != 0) {
        child_regs->sp = newsp;
      }

      // Handle TID pointers
      if (flags & CLONE_PARENT_SETTID) {
        uint64_t parent_tidptr = regs->x[2];
        uint64_t pa = translate_user_va(parent, parent_tidptr);
        if (pa != 0) {
          *(int*)pa = child->tid;
        }
      }

      if (flags & CLONE_CHILD_SETTID) {
        uint64_t child_tidptr = regs->x[4];
        uint64_t pa = translate_user_va(child, child_tidptr);
        if (pa != 0) {
          *(int*)pa = child->tid;
        }
      }

      if (flags & CLONE_CHILD_CLEARTID) {
        child->clear_child_tid = regs->x[4]; // child_tidptr is in x4 for sys_clone
      }

      child->regs = child_regs;

      // Set up CPU context for scheduler to switch to child
      CbMemSet((int8_t*)&child->context, 0, sizeof(CpuContext));
      child->context.sp = (uint64_t)child_regs;
      
      extern void fork_child_restore(void);
      child->context.lr = (uint64_t)fork_child_restore;
      child->context.fp = (uint64_t)child_regs;

      child->state = THREAD_STATE_READY;

      pl011_puts("Forked thread tid=");
      print_hex(child->tid);
      pl011_puts(" parent=");
      print_hex(parent->tid);
      pl011_puts("\n");

      IntEnable();
      return child->tid; // Parent returns child's TID
    }
  }
  IntEnable();
  return -1; // EAGAIN
}



