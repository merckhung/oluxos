#ifndef ASM_PGTABLE_H
#define ASM_PGTABLE_H

#include <olux/const.h>

#include <asm/memory.h>
#include <asm/sysreg.h>

#define PTE_VALID (UL(1) << 0)
#define PTE_TABLE (UL(1) << 1) /* at L1/L2: table; at L3: page */
#define PTE_PAGE (UL(1) << 1)
#define PTE_ATTRINDX(n) ((n) << 2)
#define PTE_ATTRINDX_MASK (UL(7) << 2)
#define PTE_NS (UL(1) << 5)
#define PTE_USER (UL(1) << 6)   /* AP[1]: EL0 access */
#define PTE_RDONLY (UL(1) << 7) /* AP[2]: read-only */
#define PTE_SH_INNER (UL(3) << 8)
#define PTE_AF (UL(1) << 10)
#define PTE_NG (UL(1) << 11)
#define PTE_DBM (UL(1) << 51)
#define PTE_CONT (UL(1) << 52)
#define PTE_PXN (UL(1) << 53)
#define PTE_UXN (UL(1) << 54)
/* Software bits (ignored by hardware) */
#define PTE_SW_COW (UL(1) << 55)
#define PTE_SW_SHARED (UL(1) << 56) /* not owned by this address space */
#define PTE_SW_WRITE (UL(1) << 57)  /* logically writable (COW pages are RDONLY) */

#define PTE_ADDR_MASK UL(0x0000fffffffff000)

#define PT_ENTRIES 512
#define L1_SHIFT 30
#define L2_SHIFT 21
#define L3_SHIFT 12
#define L1_SIZE (UL(1) << L1_SHIFT)
#define L2_SIZE (UL(1) << L2_SHIFT)
#define L1_INDEX(va) (((va) >> L1_SHIFT) & (PT_ENTRIES - 1))
#define L2_INDEX(va) (((va) >> L2_SHIFT) & (PT_ENTRIES - 1))
#define L3_INDEX(va) (((va) >> L3_SHIFT) & (PT_ENTRIES - 1))

/* Kernel mapping attributes */
#define PROT_SECT_NORMAL (PTE_VALID | PTE_ATTRINDX(MT_NORMAL) | PTE_SH_INNER | PTE_AF)
#define PROT_KERNEL_RW (PTE_VALID | PTE_PAGE | PTE_ATTRINDX(MT_NORMAL) | PTE_SH_INNER | PTE_AF | PTE_PXN | PTE_UXN)
#define PROT_KERNEL_RO (PROT_KERNEL_RW | PTE_RDONLY)
#define PROT_KERNEL_RX (PTE_VALID | PTE_PAGE | PTE_ATTRINDX(MT_NORMAL) | PTE_SH_INNER | PTE_AF | PTE_RDONLY | PTE_UXN)
#define PROT_KERNEL_RWX (PTE_VALID | PTE_PAGE | PTE_ATTRINDX(MT_NORMAL) | PTE_SH_INNER | PTE_AF | PTE_UXN)
#define PROT_DEVICE (PTE_VALID | PTE_PAGE | PTE_ATTRINDX(MT_DEVICE_nGnRE) | PTE_AF | PTE_PXN | PTE_UXN)
#define PROT_NORMAL_NC (PTE_VALID | PTE_PAGE | PTE_ATTRINDX(MT_NORMAL_NC) | PTE_SH_INNER | PTE_AF | PTE_PXN | PTE_UXN)

/* User mapping attributes (all non-global, tagged with the ASID) */
#define PROT_USER_BASE (PTE_VALID | PTE_PAGE | PTE_ATTRINDX(MT_NORMAL) | PTE_SH_INNER | PTE_AF | PTE_NG | PTE_USER | PTE_PXN)

#ifndef __ASSEMBLY__
#include <olux/types.h>
typedef u64 pte_t;

static inline phys_addr_t pte_addr(pte_t p) { return p & PTE_ADDR_MASK; }
static inline bool pte_valid(pte_t p) { return p & PTE_VALID; }
static inline bool pte_is_table(pte_t p) { return (p & 3) == 3; }
static inline bool pte_is_block(pte_t p) { return (p & 3) == 1; }

extern pte_t swapper_pg_dir[PT_ENTRIES]; /* TTBR1 L1 */
extern pte_t empty_zero_pg_dir[PT_ENTRIES];

static inline void flush_tlb_all(void) {
  __asm__ volatile("dsb ishst\n tlbi vmalle1is\n dsb ish\n isb" ::: "memory");
}
static inline void flush_tlb_kernel_page(u64 va) {
  __asm__ volatile("dsb ishst\n tlbi vaale1is, %0\n dsb ish\n isb" ::"r"(va >> 12) : "memory");
}
static inline void flush_tlb_asid(u16 asid) {
  __asm__ volatile("dsb ishst\n tlbi aside1is, %0\n dsb ish\n isb" ::"r"((u64)asid << 48) : "memory");
}
static inline void flush_tlb_user_page(u16 asid, u64 va) {
  __asm__ volatile("dsb ishst\n tlbi vae1is, %0\n dsb ish\n isb" ::"r"(((u64)asid << 48) | ((va >> 12) & UL(0xfffffffffff))) : "memory");
}
#endif

#endif
