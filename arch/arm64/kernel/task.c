#include <arm64/interrupt.h>
#include <arm64/task.h>
#include <clib.h>
#include <types.h>

void pl011_puts(const char* s);
void print_hex(uint64_t val);
void thread_entry_wrapper(void);  // Assembly wrapper

static Thread threads[MAX_THREADS];
static uint8_t thread_stacks[MAX_THREADS][STACK_SIZE]
    __attribute__((aligned(16)));
static Thread* current_thread = NULL;

extern volatile uint64_t pg_dir[512];

static uint64_t thread_l1_tables[MAX_THREADS][512]
    __attribute__((aligned(4096)));
static uint64_t thread_l2_tables[MAX_THREADS][512]
    __attribute__((aligned(4096)));
static uint64_t thread_l3_tables[MAX_THREADS][512]
    __attribute__((aligned(4096)));

void thread_init(void) {
  int i;
  for (i = 0; i < MAX_THREADS; i++) {
    threads[i].state = THREAD_STATE_FREE;
    threads[i].tid = i;
    threads[i].stack_base = thread_stacks[i];
    threads[i].stack_size = STACK_SIZE;
    threads[i].pg_dir_phys = (uint64_t)pg_dir;  // Default to boot page table
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

    // Switch page tables
    uint64_t next_pg_dir = next->pg_dir_phys;
    __asm__ volatile(
        "msr ttbr0_el1, %0\n"
        "tlbi vmalle1is\n"
        "dsb sy\n"
        "isb\n" ::"r"(next_pg_dir)
        : "memory");

    cpu_switch_to(&prev->context, &next->context);
  } else {
    if (current_thread->state == THREAD_STATE_RUNNING) {
      return;
    }
    // Idle loop
    IntEnable();
    __asm__ volatile("wfi");
    IntDisable();
    goto retry;
  }
}

void map_page_thread(Thread* t, uint64_t vaddr, uint64_t paddr,
                     uint64_t flags) {
  uint32_t idx = (vaddr & 0x1FFFFF) / 4096;  // Offset within first 2MB
  uint64_t* l3 = thread_l3_tables[t->tid];
  l3[idx] = (paddr & ~0xFFF) | flags;
}

#define USER_CODE_SIZE 32768
static uint8_t user_code_pages[MAX_THREADS][USER_CODE_SIZE]
    __attribute__((aligned(4096)));
static uint8_t user_stack_pages[MAX_THREADS][4096]
    __attribute__((aligned(4096)));

extern void userspace_entry_wrapper(void);

int thread_create_userspace(const unsigned char* bin, uint32_t size) {
  int i;
  IntDisable();
  for (i = 1; i < MAX_THREADS; i++) {
    if (threads[i].state == THREAD_STATE_FREE) {
      Thread* t = &threads[i];

      if (size > USER_CODE_SIZE) {
        pl011_puts("User binary too large!\n");
        IntEnable();
        return -1;
      }

      // Initialize private page tables for this thread
      uint64_t* l1 = thread_l1_tables[i];
      uint64_t* l2 = thread_l2_tables[i];
      uint64_t* l3 = thread_l3_tables[i];

      CbMemSet((int8_t*)l1, 0, 4096);
      CbMemSet((int8_t*)l2, 0, 4096);
      CbMemSet((int8_t*)l3, 0, 4096);

      // L1[0] -> L2
      l1[0] = ((uint64_t)l2 & ~0xFFF) | 0x3;
      // L1[1] -> copy RAM mapping from boot pg_dir[1]
      l1[1] = pg_dir[1];

      // L2[0] -> L3
      l2[0] = ((uint64_t)l3 & ~0xFFF) | 0x3;
      // L2[64] -> GIC
      l2[64] = 0x0060000008000401ULL;
      // L2[72] -> UART
      l2[72] = 0x0060000009000401ULL;

      t->pg_dir_phys = (uint64_t)l1;

      CbMemCpy(user_code_pages[i], bin, size);

      // Flags for user code: PXN=1, UXN=0, AP=01 (RW EL1/EL0), SH=11, AF=1,
      // Attr=1, Type=3
      uint64_t code_flags = 0x0020000000000747ULL;
      uint32_t num_pages = USER_CODE_SIZE / 4096;
      uint32_t p;
      for (p = 0; p < num_pages; p++) {
        uint64_t vaddr = 0x00100000 + p * 4096;
        uint64_t paddr = (uint64_t)user_code_pages[i] + p * 4096;
        map_page_thread(t, vaddr, paddr, code_flags);
      }

      // Flags for user stack: PXN=1, UXN=1, AP=01 (RW EL1/EL0), SH=11, AF=1,
      // Attr=1, Type=3
      uint64_t stack_flags = 0x0060000000000747ULL;
      map_page_thread(t, 0x001FF000, (uint64_t)user_stack_pages[i],
                      stack_flags);

      uint8_t* stk_top = (uint8_t*)t->stack_base + t->stack_size;
      stk_top -= sizeof(CpuContext);
      CpuContext* ctx = (CpuContext*)stk_top;

      CbMemSet((int8_t*)ctx, 0, sizeof(CpuContext));

      ctx->lr = (uint64_t)userspace_entry_wrapper;
      ctx->sp = (uint64_t)stk_top;
      ctx->fp = (uint64_t)stk_top;

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
  } else if (phys_addr == 0x48000000) {
    uint64_t virt_base = 0x10000000;
    uint64_t phys_base = 0x48000000;
    uint32_t num_blocks = 17;  // 34MB

    uint64_t* l1 = (uint64_t*)current_thread->pg_dir_phys;
    uint64_t* l2 = (uint64_t*)(l1[0] & ~0xFFF);

    uint32_t start_l2_idx = virt_base >> 21;
    uint32_t i;

    for (i = 0; i < num_blocks; i++) {
      uint32_t idx = start_l2_idx + i;
      uint64_t paddr = phys_base + i * 0x200000;
      l2[idx] = paddr | 0x0060000000000745ULL;
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
