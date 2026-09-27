/*
 * User copy routines built on unprivileged loads/stores. Every instruction
 * that touches user memory has an __ex_table entry; a fault there resumes
 * at the fixup, which returns the number of bytes left.
 */
  .macro uentry insn, fixup
  .pushsection __ex_table, "a"
  .balign 8
  .quad \insn, \fixup
  .popsection
  .endm

  .text
/* unsigned long __arch_copy_from_user(void *to, u64 from, unsigned long n) */
  .globl __arch_copy_from_user
__arch_copy_from_user:
  orr     x4, x0, x1
  tst     x4, #7
  b.ne    2f
1:cmp     x2, #8
  b.lo    2f
10:ldtr   x3, [x1]
  uentry  10b, 9f
  str     x3, [x0], #8
  add     x1, x1, #8
  sub     x2, x2, #8
  b       1b
2:cbz     x2, 3f
11:ldtrb  w3, [x1]
  uentry  11b, 9f
  strb    w3, [x0], #1
  add     x1, x1, #1
  sub     x2, x2, #1
  b       2b
3:mov     x0, #0
  ret
9:mov     x0, x2
  ret

/* unsigned long __arch_copy_to_user(u64 to, const void *from, unsigned long n) */
  .globl __arch_copy_to_user
__arch_copy_to_user:
  orr     x4, x0, x1
  tst     x4, #7
  b.ne    2f
1:cmp     x2, #8
  b.lo    2f
  ldr     x3, [x1], #8
10:sttr   x3, [x0]
  uentry  10b, 8f
  add     x0, x0, #8
  sub     x2, x2, #8
  b       1b
2:cbz     x2, 3f
  ldrb    w3, [x1], #1
11:sttrb  w3, [x0]
  uentry  11b, 9f
  add     x0, x0, #1
  sub     x2, x2, #1
  b       2b
3:mov     x0, #0
  ret
8:mov     x0, x2              /* 8-byte store faulted: nothing of it copied */
  ret
9:mov     x0, x2
  ret

/* unsigned long __arch_clear_user(u64 to, unsigned long n) */
  .globl __arch_clear_user
__arch_clear_user:
  cbz     x1, 2f
10:sttrb  wzr, [x0]
  uentry  10b, 9f
  add     x0, x0, #1
  sub     x1, x1, #1
  b       __arch_clear_user
2:mov     x0, #0
  ret
9:mov     x0, x1
  ret

/* long __arch_strnlen_user(u64 s, long max): length including the NUL,
 * max+1 if no NUL within max bytes, 0 on fault. */
  .globl __arch_strnlen_user
__arch_strnlen_user:
  mov     x2, #0
1:cmp     x2, x1
  b.ge    3f
10:ldtrb  w3, [x0]
  uentry  10b, 9f
  add     x0, x0, #1
  add     x2, x2, #1
  cbnz    w3, 1b
  mov     x0, x2
  ret
3:add     x0, x1, #1
  ret
9:mov     x0, #0
  ret
