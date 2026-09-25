/* Context switch and FP/SIMD state save/restore. */

/*
 * struct thread *cpu_switch_to(struct thread *prev, struct thread *next)
 * Saves callee-saved registers, SP and the return address into prev->ctx
 * (offset 0 of struct thread) and restores next's. Returns prev in x0 on
 * next's stack.
 */
  .text
  .globl cpu_switch_to
cpu_switch_to:
  mov     x9, sp
  stp     x19, x20, [x0, #0]
  stp     x21, x22, [x0, #16]
  stp     x23, x24, [x0, #32]
  stp     x25, x26, [x0, #48]
  stp     x27, x28, [x0, #64]
  stp     x29, x9, [x0, #80]
  str     x30, [x0, #96]
  ldp     x19, x20, [x1, #0]
  ldp     x21, x22, [x1, #16]
  ldp     x23, x24, [x1, #32]
  ldp     x25, x26, [x1, #48]
  ldp     x27, x28, [x1, #64]
  ldp     x29, x9, [x1, #80]
  ldr     x30, [x1, #96]
  mov     sp, x9
  ret

/* void fpsimd_save(struct fpsimd_state *st) */
  .globl fpsimd_save
fpsimd_save:
  stp     q0, q1, [x0, #0 * 32]
  stp     q2, q3, [x0, #1 * 32]
  stp     q4, q5, [x0, #2 * 32]
  stp     q6, q7, [x0, #3 * 32]
  stp     q8, q9, [x0, #4 * 32]
  stp     q10, q11, [x0, #5 * 32]
  stp     q12, q13, [x0, #6 * 32]
  stp     q14, q15, [x0, #7 * 32]
  stp     q16, q17, [x0, #8 * 32]
  stp     q18, q19, [x0, #9 * 32]
  stp     q20, q21, [x0, #10 * 32]
  stp     q22, q23, [x0, #11 * 32]
  stp     q24, q25, [x0, #12 * 32]
  stp     q26, q27, [x0, #13 * 32]
  stp     q28, q29, [x0, #14 * 32]
  stp     q30, q31, [x0, #15 * 32]
  mrs     x1, fpsr
  mrs     x2, fpcr
  add     x0, x0, #512
  stp     w1, w2, [x0]
  ret

/* void fpsimd_load(const struct fpsimd_state *st) */
  .globl fpsimd_load
fpsimd_load:
  ldp     q0, q1, [x0, #0 * 32]
  ldp     q2, q3, [x0, #1 * 32]
  ldp     q4, q5, [x0, #2 * 32]
  ldp     q6, q7, [x0, #3 * 32]
  ldp     q8, q9, [x0, #4 * 32]
  ldp     q10, q11, [x0, #5 * 32]
  ldp     q12, q13, [x0, #6 * 32]
  ldp     q14, q15, [x0, #7 * 32]
  ldp     q16, q17, [x0, #8 * 32]
  ldp     q18, q19, [x0, #9 * 32]
  ldp     q20, q21, [x0, #10 * 32]
  ldp     q22, q23, [x0, #11 * 32]
  ldp     q24, q25, [x0, #12 * 32]
  ldp     q26, q27, [x0, #13 * 32]
  ldp     q28, q29, [x0, #14 * 32]
  ldp     q30, q31, [x0, #15 * 32]
  add     x0, x0, #512
  ldp     w1, w2, [x0]
  msr     fpsr, x1
  msr     fpcr, x2
  ret
