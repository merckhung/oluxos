#include <arm64/platform.h>
#include <arm64/task.h>
#include <driver/arm_timer.h>
#include <driver/gicv2.h>
#include <types.h>
#include <driver/fb.h>

// Include generated userspace binary
#include "user_shell_bin.h"

void pl011_init(void);
void pl011_puts(const char* s);
void pl011_putc(char c);

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

  uint64_t esr, far;
  __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
  __asm__ volatile("mrs %0, far_el1" : "=r"(far));

  pl011_puts("ESR_EL1: ");
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
    pl011_puts("\n");
    exception_handler_dump(regs->pc, "EL0 Fault");
  }

  thread_set_current_regs(regs);

  uint64_t syscall_num = regs->x[8];
  uint64_t arg0 = regs->x[0];
  uint64_t arg1 = regs->x[1];
  uint64_t arg2 = regs->x[2];

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
    int ret = thread_ipc_send((uint32_t)arg0, (void*)arg1, (uint32_t)arg2);
    if (ret != IPC_BLOCKED) {
      regs->x[0] = ret;
    }
  } else if (syscall_num == 3) {  // SYS_RECV
    int ret = thread_ipc_recv((uint32_t)arg0, (void*)arg1, (uint32_t)arg2);
    if (ret != IPC_BLOCKED) {
      regs->x[0] = ret;
    }
  } else if (syscall_num == 4) {  // SYS_GETTID
    regs->x[0] = thread_get_current_tid();
  } else if (syscall_num == 5) {  // SYS_MAP_MMIO
    regs->x[0] = (uint64_t)thread_map_mmio(arg0);
  } else if (syscall_num == 6) {  // SYS_MAP_FB
    regs->x[0] = (uint64_t)thread_map_fb();
  } else {
    pl011_puts("Unknown syscall: ");
    print_hex(syscall_num);
    pl011_puts("\n");
    regs->x[0] = -1;  // Error
  }
}

void krn_entry(void) {
  pl011_init();
  pl011_puts("\n\n");
  pl011_puts("====================================\n");
  pl011_puts(" OluxOS ARM64 Starting...\n");
  pl011_puts("====================================\n");
  pl011_puts("Booted successfully to EL1.\n");
  fb_init();

  gicv2_init();
  gicv2_enable_irq(30);
  arm_timer_init(100);

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
