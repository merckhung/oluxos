/*
 * Exception vectors and kernel entry/exit.
 *
 * Every exception saves a full struct pt_regs on the current kernel stack.
 * Returns to user space go through ret_to_user, which delivers pending
 * signals and reschedules with interrupts masked, so no wakeup is lost
 * between the check and the eret.
 */
#include <asm/ptrace.h>
#include <asm/sysreg.h>

  .macro kernel_entry el
  sub     sp, sp, #S_FRAME_SIZE
  stp     x0, x1, [sp, #16 * 0]
  stp     x2, x3, [sp, #16 * 1]
  stp     x4, x5, [sp, #16 * 2]
  stp     x6, x7, [sp, #16 * 3]
  stp     x8, x9, [sp, #16 * 4]
  stp     x10, x11, [sp, #16 * 5]
  stp     x12, x13, [sp, #16 * 6]
  stp     x14, x15, [sp, #16 * 7]
  stp     x16, x17, [sp, #16 * 8]
  stp     x18, x19, [sp, #16 * 9]
  stp     x20, x21, [sp, #16 * 10]
  stp     x22, x23, [sp, #16 * 11]
  stp     x24, x25, [sp, #16 * 12]
  stp     x26, x27, [sp, #16 * 13]
  stp     x28, x29, [sp, #16 * 14]
  .if \el == 0
  mrs     x21, sp_el0
  .else
  add     x21, sp, #S_FRAME_SIZE
  .endif
  mrs     x22, elr_el1
  mrs     x23, spsr_el1
  stp     x30, x21, [sp, #S_LR]
  stp     x22, x23, [sp, #S_PC]
  mov     x0, #-1
  str     x0, [sp, #S_SYSCALLNO]
  /* frame record so backtraces can walk through exceptions */
  .if \el == 0
  stp     xzr, xzr, [sp, #S_STACKFRAME]
  .else
  stp     x29, x22, [sp, #S_STACKFRAME]
  .endif
  add     x29, sp, #S_STACKFRAME
  .endm

  .macro kernel_exit el
  ldp     x21, x22, [sp, #S_PC]   /* x21 = pc, x22 = pstate */
  .if \el == 0
  ldr     x23, [sp, #S_SP]
  msr     sp_el0, x23
  .endif
  msr     elr_el1, x21
  msr     spsr_el1, x22
  ldp     x0, x1, [sp, #16 * 0]
  ldp     x2, x3, [sp, #16 * 1]
  ldp     x4, x5, [sp, #16 * 2]
  ldp     x6, x7, [sp, #16 * 3]
  ldp     x8, x9, [sp, #16 * 4]
  ldp     x10, x11, [sp, #16 * 5]
  ldp     x12, x13, [sp, #16 * 6]
  ldp     x14, x15, [sp, #16 * 7]
  ldp     x16, x17, [sp, #16 * 8]
  ldp     x18, x19, [sp, #16 * 9]
  ldp     x20, x21, [sp, #16 * 10]
  ldp     x22, x23, [sp, #16 * 11]
  ldp     x24, x25, [sp, #16 * 12]
  ldp     x26, x27, [sp, #16 * 13]
  ldp     x28, x29, [sp, #16 * 14]
  ldr     x30, [sp, #S_LR]
  add     sp, sp, #S_FRAME_SIZE
  eret
  .endm

  .macro ventry label
  .balign 128
  b       \label
  .endm

  .text
  .balign 2048
  .globl vectors
vectors:
  ventry  el1t_sync_invalid       /* EL1 with SP_EL0: never used */
  ventry  el1t_irq_invalid
  ventry  el1t_fiq_invalid
  ventry  el1t_error_invalid
  ventry  el1h_sync               /* EL1 with SP_EL1 */
  ventry  el1h_irq
  ventry  el1h_fiq_invalid
  ventry  el1h_error
  ventry  el0_sync                /* EL0 AArch64 */
  ventry  el0_irq
  ventry  el0_fiq_invalid
  ventry  el0_error
  ventry  el0_32_invalid          /* EL0 AArch32: unsupported */
  ventry  el0_32_invalid
  ventry  el0_32_invalid
  ventry  el0_32_invalid

  .macro invalid name, el, reason
\name:
  kernel_entry \el
  mov     x0, sp
  mov     x1, #\reason
  mrs     x2, esr_el1
  bl      bad_mode
  b       .
  .endm

  invalid el1t_sync_invalid, 1, 0
  invalid el1t_irq_invalid, 1, 1
  invalid el1t_fiq_invalid, 1, 2
  invalid el1t_error_invalid, 1, 3
  invalid el1h_fiq_invalid, 1, 2
  invalid el0_fiq_invalid, 0, 2
  invalid el0_32_invalid, 0, 4

el1h_sync:
  kernel_entry 1
  mov     x0, sp
  bl      el1_sync_handler
  kernel_exit 1

el1h_irq:
  kernel_entry 1
  mov     x0, sp
  bl      irq_handler
  kernel_exit 1

el1h_error:
  kernel_entry 1
  mov     x0, sp
  mrs     x1, esr_el1
  bl      serror_handler
  kernel_exit 1

el0_sync:
  kernel_entry 0
  mov     x0, sp
  bl      el0_sync_handler
  b       ret_to_user

el0_irq:
  kernel_entry 0
  mov     x0, sp
  bl      irq_handler
  b       ret_to_user

el0_error:
  kernel_entry 0
  mov     x0, sp
  mrs     x1, esr_el1
  bl      serror_handler
  b       ret_to_user

  .globl ret_to_user
ret_to_user:
  msr     daifset, #2
  mov     x0, sp
  bl      prepare_exit_to_user    /* signals, reschedule; returns with IRQs off */
  kernel_exit 0

/* First return of a newly created user thread (fork, exec, clone). */
  .globl ret_from_fork
ret_from_fork:
  bl      schedule_tail
  cbz     x19, 1f                 /* x19 = kernel thread function, if any */
  mov     x0, x20
  blr     x19
1:b       ret_to_user
