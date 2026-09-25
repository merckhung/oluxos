/*
 * Address-space switching and ASID allocation. Every user address space
 * gets its own ASID for its lifetime, so context switches need no TLB
 * flush; freed ASIDs are invalidated before reuse.
 */
#include <asm/pgtable.h>
#include <olux/kernel.h>
#include <olux/mmu_context.h>
#include <olux/spinlock.h>
#include <olux/vm.h>

#define MAX_ASIDS 65536
static u64 asid_map[MAX_ASIDS / 64];
static unsigned asid_limit;
static unsigned asid_next = 1;
static DEFINE_SPINLOCK(asid_lock);

int asid_alloc(void) {
  unsigned long f = spin_lock_irqsave(&asid_lock);
  if (!asid_limit) {
    u64 mmfr0 = read_sysreg(id_aa64mmfr0_el1);
    asid_limit = ((mmfr0 >> 4) & 0xf) == 2 ? MAX_ASIDS : 256;
    asid_map[0] |= 1; /* ASID 0 is reserved for kernel threads */
  }
  for (unsigned n = 0; n < asid_limit; n++) {
    unsigned a = asid_next;
    asid_next = asid_next + 1 >= asid_limit ? 1 : asid_next + 1;
    if (!(asid_map[a / 64] & (1UL << (a % 64)))) {
      asid_map[a / 64] |= 1UL << (a % 64);
      spin_unlock_irqrestore(&asid_lock, f);
      return (int)a;
    }
  }
  spin_unlock_irqrestore(&asid_lock, f);
  return -1;
}

void asid_free(u16 asid) {
  if (!asid) return;
  flush_tlb_asid(asid); /* no stale translations may survive reuse */
  unsigned long f = spin_lock_irqsave(&asid_lock);
  asid_map[asid / 64] &= ~(1UL << (asid % 64));
  spin_unlock_irqrestore(&asid_lock, f);
}

void switch_mm(struct mm *prev, struct mm *next) {
  if (prev == next) return;
  u64 ttbr = next ? next->pgd_pa | ((u64)next->asid << 48) : virt_to_phys(empty_zero_pg_dir);
  write_sysreg(ttbr0_el1, ttbr);
  isb();
}
