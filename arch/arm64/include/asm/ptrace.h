#ifndef ASM_PTRACE_H
#define ASM_PTRACE_H

/* Exception frame saved on the kernel stack on every entry. */
#define S_X0 0
#define S_LR (30 * 8)
#define S_SP (31 * 8)
#define S_PC (32 * 8)
#define S_PSTATE (33 * 8)
#define S_ORIG_X0 (34 * 8)
#define S_SYSCALLNO (35 * 8)
#define S_STACKFRAME (36 * 8)
#define S_FRAME_SIZE (38 * 8)

#ifndef __ASSEMBLY__
#include <olux/types.h>

struct pt_regs {
  u64 regs[31];
  u64 sp; /* SP_EL0 for user frames, pre-exception SP for kernel frames */
  u64 pc;
  u64 pstate;
  u64 orig_x0;
  s64 syscallno;
  u64 stackframe[2]; /* frame record {fp, pc} for unwinding across traps */
};

_Static_assert(sizeof(struct pt_regs) == S_FRAME_SIZE, "pt_regs layout");

static inline bool user_mode(const struct pt_regs *r) { return (r->pstate & 0xf) == 0; }
#endif

#endif
