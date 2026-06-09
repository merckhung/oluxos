#include <arm64/platform.h>
#include <arm64/task.h>
#include <driver/arm_timer.h>
#include <driver/gicv2.h>
#include <types.h>
#include <driver/fb.h>
#include <arm64/kdbger.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>
#include <kernel/vmm.h>

extern char _stack_top[];

#if CONFIG_BOARD_RPI4
#define UART_IRQ 153
#else
#define UART_IRQ 33
#endif


// Include generated userspace binary
#if CONFIG_BOARD_RPI4
#include "user_shell_bin_rpi4.h"
#else
#include "user_shell_bin.h"
#endif

#include <kernel/console.h>

void pl011_init(void);
void pl011_puts(const char* s);
void pl011_putc(char c);
char pl011_getc(void);
extern void thread_exit(void);

void kputs(const char* s) {
  pl011_puts(s);
}

void print_hex(uint64_t val) {
  char hex[17];
  int i;
  for (i = 15; i >= 0; i--) {
    int digit = val & 0xF;
    hex[i] = digit < 10 ? '0' + digit : 'A' + digit - 10;
    val >>= 4;
  }
  hex[16] = '\0';
  pl011_puts("0x");
  pl011_puts(hex);
}

void exception_handler_dump(uint64_t lr, const char* msg) {
  pl011_puts("\n!!! EXCEPTION !!!\n");
  pl011_puts(msg);
  pl011_puts("\nLR: ");
  print_hex(lr);
  pl011_puts("\n");

  uint64_t esr, far, elr;
  __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
  __asm__ volatile("mrs %0, far_el1" : "=r"(far));
  __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));

  pl011_puts("ELR_EL1: ");
  print_hex(elr);
  pl011_puts("\nESR_EL1: ");
  print_hex(esr);
  pl011_puts("\nFAR_EL1: ");
  print_hex(far);
  pl011_puts("\n");

  while (1);
}

void irq_handler_el1(ARM64Registers* regs) {
  uint32_t irq = gicv2_acknowledge_irq();

  if (irq == 30) {
    arm_timer_reset(100);
    gicv2_end_of_irq(irq);
  } else if (irq == UART_IRQ) {
    kdbger_intr_handler();
    gicv2_end_of_irq(irq);
  } else {
    pl011_puts("Unexpected EL1 IRQ: ");
    print_hex(irq);
    pl011_puts("\n");
    gicv2_end_of_irq(irq);
  }
}

void irq_handler_el0(ARM64Registers* regs) {
  uint32_t irq = gicv2_acknowledge_irq();

  if (irq == 30) {
    arm_timer_reset(100);
    thread_set_current_regs(regs);
    gicv2_end_of_irq(irq);  // EOI before schedule
    schedule();
  } else if (irq == UART_IRQ) {
    kdbger_intr_handler();
    gicv2_end_of_irq(irq);
  } else {
    pl011_puts("Unexpected EL0 IRQ: ");
    print_hex(irq);
    pl011_puts("\n");
    exception_handler_dump(regs->pc, "EL0 IRQ Fault");
    gicv2_end_of_irq(irq);
  }
}

void syscall_handler(ARM64Registers* regs) {
  uint64_t esr;
  __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
  uint32_t ec = (esr >> 26) & 0x3F;

  if (ec != 0x15) {  // 0x15 is SVC in AArch64
    pl011_puts("\n!!! EL0 SYNC EXCEPTION (not syscall) !!!\n");
    pl011_puts("EC: ");
    print_hex(ec);
    pl011_puts(" ISS: ");
    print_hex(esr & 0x1FFFFFF);
    pl011_puts("\nEL0 PC: ");
    print_hex(regs->pc);
    pl011_puts("\nEL0 LR: ");
    print_hex(regs->x[30]);
    pl011_puts("\nEL0 SP: ");
    print_hex(regs->sp);
    pl011_puts("\n");
    exception_handler_dump(regs->pc, "EL0 Fault");
  }

  thread_set_current_regs(regs);

  uint64_t syscall_num = regs->x[8];
  uint64_t arg0 = regs->x[0];
  uint64_t arg1 = regs->x[1];
  uint64_t arg2 = regs->x[2];
  uint64_t arg3 = regs->x[3];
  uint64_t arg4 = regs->x[4];
  uint64_t arg5 = regs->x[5];

  /*
  pl011_puts("SC ");
  print_hex(syscall_num);
  pl011_puts(" x0=");
  print_hex(arg0);
  pl011_puts(" x1=");
  print_hex(arg1);
  pl011_puts(" PC=");
  print_hex(regs->pc);
  pl011_puts("\n");
  */

  if (syscall_num == 1) {  // SYS_PUTCHAR
    pl011_putc((char)arg0);
    regs->x[0] = 0;               // Success
  } else if (syscall_num == 2) {  // SYS_SEND
    /*
    pl011_puts("KRN: SYS_SEND from ");
    print_hex(thread_get_current_tid());
    pl011_puts(" to ");
    print_hex(arg0);
    pl011_puts("\n");
    */
    int ret = thread_ipc_send((uint32_t)arg0, (void*)arg1, (uint32_t)arg2);
    if (ret != IPC_BLOCKED) {
      regs->x[0] = ret;
    }
  } else if (syscall_num == 3) {  // SYS_RECV
    /*
    pl011_puts("KRN: SYS_RECV from ");
    print_hex(thread_get_current_tid());
    pl011_puts(" src ");
    print_hex(arg0);
    pl011_puts("\n");
    */
    int ret = thread_ipc_recv((uint32_t)arg0, (void*)arg1, (uint32_t)arg2);
    if (ret != IPC_BLOCKED) {
      regs->x[0] = ret;
    }
    /*
    pl011_puts("KRN: SYS_RECV exit, tid=");
    print_hex(thread_get_current_tid());
    pl011_puts(" x0=");
    print_hex(regs->x[0]);
    pl011_puts(" x30=");
    print_hex(regs->x[30]);
    pl011_puts("\n");
    */
  } else if (syscall_num == 4) {  // SYS_GETTID
    regs->x[0] = thread_get_current_tid();
  } else if (syscall_num == 5) {  // SYS_MAP_MMIO
    regs->x[0] = (uint64_t)thread_map_mmio(arg0);
  } else if (syscall_num == 6) {  // SYS_MAP_FB
    regs->x[0] = (uint64_t)thread_map_fb();
  } else if (syscall_num == 7) {  // SYS_SPAWN
    regs->x[0] = thread_create_userspace((const unsigned char*)arg0, (uint32_t)arg1);
  } else if (syscall_num == 64) { // sys_write
    if (arg0 == 1 || arg0 == 2) {
      char* buf = (char*)arg1;
      uint64_t i;
      for (i = 0; i < arg2; i++) {
        char c = buf[i];
        if (c == '\n') pl011_putc('\r');
        pl011_putc(c);
      }
      regs->x[0] = arg2;
    } else {
      regs->x[0] = (uint64_t)-9; // EBADF
    }
  } else if (syscall_num == 63) { // sys_read
    if (arg0 == 0) { // stdin
      char* buf = (char*)arg1;
      uint64_t len = arg2;
      uint64_t count = 0;
      if (len > 0) {
        buf[count++] = pl011_getc(); // Block for first character
        // Read remaining characters if available
        while (count < len) {
          volatile unsigned int* uart_fr = (volatile unsigned int*)(0x09000000ULL + 0x18);
          if (*uart_fr & (1 << 4)) break; // RX FIFO empty
          buf[count++] = pl011_getc();
        }
        regs->x[0] = count;
      } else {
        regs->x[0] = 0;
      }
    } else {
      regs->x[0] = (uint64_t)-9; // EBADF
    }
  } else if (syscall_num == 66) { // sys_writev
    if (arg0 == 1 || arg0 == 2) {
      uint64_t* iov = (uint64_t*)arg1;
      uint64_t iovcnt = arg2;
      uint64_t total = 0;
      uint64_t i;
      for (i = 0; i < iovcnt; i++) {
        char* buf = (char*)iov[i*2];
        uint64_t len = iov[i*2 + 1];
        uint64_t j;
        for (j = 0; j < len; j++) {
          char c = buf[j];
          if (c == '\n') pl011_putc('\r');
          pl011_putc(c);
        }
        total += len;
      }
      regs->x[0] = total;
    } else {
      regs->x[0] = (uint64_t)-9; // EBADF
    }
  } else if (syscall_num == 64) { // sys_write
    if (arg0 == 1 || arg0 == 2) {
      char* buf = (char*)arg1;
      uint64_t len = arg2;
      uint64_t i;
      for (i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n') pl011_putc('\r');
        pl011_putc(c);
      }
      regs->x[0] = len;
    } else {
      regs->x[0] = (uint64_t)-9; // EBADF
    }
  } else if (syscall_num == 93 || syscall_num == 94) { // sys_exit / sys_exit_group
    pl011_puts("Linux process exited.\n");
    thread_exit();
  } else if (syscall_num == 29) { // sys_ioctl
    // arg0 = fd, arg1 = cmd
    if (arg1 == 0x5401 || arg1 == 0x5413) { // TCGETS / TIOCGWINSZ
      regs->x[0] = (uint64_t)-25; // ENOTTY (to make ash fall back to simple terminal)
    } else {
      regs->x[0] = (uint64_t)-25; // ENOTTY
    }
  } else if (syscall_num == 214) { // sys_brk
    pl011_puts("sys_brk arg0="); print_hex(arg0); pl011_puts("\n");
    if (arg0 == 0) regs->x[0] = 0x00510000;
    else regs->x[0] = arg0;
  } else if (syscall_num == 56 || syscall_num == 57 || syscall_num == 79 || syscall_num == 80 || syscall_num == 61) {
    // openat, close, newfstatat, fstat, getdents64 - stub for now
    regs->x[0] = -2; // -ENOENT
  } else if (syscall_num == 78 || syscall_num == 96 || syscall_num == 99 || syscall_num == 261 || syscall_num == 278 || syscall_num == 293 || syscall_num == 160) {
    // readlinkat, set_tid_address, set_robust_list, prlimit64, getrandom, rseq, uname
    regs->x[0] = -38; // -ENOSYS
  } else if (syscall_num == 113) { // sys_clock_gettime
    regs->x[0] = -38;
  } else if (syscall_num == 222) { // sys_mmap
    pl011_puts("sys_mmap arg0="); print_hex(arg0);
    pl011_puts(" arg1="); print_hex(arg1);
    pl011_puts(" arg4(fd)="); print_hex(arg4);
    pl011_puts("\n");
    
    extern uint64_t sys_mmap_impl(uint64_t size);
    regs->x[0] = sys_mmap_impl(arg1);
  } else if (syscall_num == 215) { // sys_munmap
    pl011_puts("sys_munmap arg0="); print_hex(arg0); pl011_puts("\n");
    regs->x[0] = 0; // Success
  } else if (syscall_num == 226) { // sys_mprotect
    regs->x[0] = 0;
  } else if (syscall_num == 63) { // sys_read
    regs->x[0] = 0; // EOF for now
  } else {
    pl011_puts("Unknown syscall: ");
    print_hex(syscall_num);
    pl011_puts("\n");
    regs->x[0] = -38;  // -ENOSYS
  }
}

void krn_entry(void) {
  pl011_init();
  pl011_puts("\n\n");
  pl011_puts("====================================\n");
  pl011_puts(" OluxOS ARM64 Starting...\n");
  pl011_puts("====================================\n");
  pl011_puts("Booted successfully to EL1.\n");

  // Initialize PMM
  uint64_t mem_start = (uint64_t)_stack_top;
  uint64_t mem_size = 0x48000000ULL - mem_start;
  pmm_init(mem_start, mem_size);

  // Initialize Heap
  heap_init();

  // Initialize VMM
  vmm_init();

  fb_init();

  gicv2_init();
  gicv2_enable_irq(30);
  arm_timer_init(100);

#if CONFIG_KDBGER
  kdbger_initialization();
  gicv2_set_irq_target(UART_IRQ, 1);
  gicv2_enable_irq(UART_IRQ);
#endif

  // Initialize threads
  thread_init();

  // Create FOUR userspace threads
  thread_create_userspace(user_shell_bin, user_shell_bin_len);
  thread_create_userspace(user_shell_bin, user_shell_bin_len);
  thread_create_userspace(user_shell_bin, user_shell_bin_len);
  thread_create_userspace(user_shell_bin, user_shell_bin_len);

  pl011_puts("Starting scheduler...\n");
  while (1) {
    schedule();
  }
}
