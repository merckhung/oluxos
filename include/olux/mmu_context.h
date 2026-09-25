#ifndef OLUX_MMU_CONTEXT_H
#define OLUX_MMU_CONTEXT_H

struct mm;
/* Install next's user page tables (TTBR0 + ASID); NULL = kernel thread. */
void switch_mm(struct mm *prev, struct mm *next);

#endif
