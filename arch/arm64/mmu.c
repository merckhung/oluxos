/*
 * Kernel address space management (TTBR1): linear map of RAM, kernel image
 * permissions (W^X), fixmap slots and generic kernel page mapping used by
 * vmalloc/ioremap.
 */
#include <asm/pgtable.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/spinlock.h>

u64 kimage_voffset;
phys_addr_t kernel_phys_start, kernel_phys_end;

pte_t empty_zero_pg_dir[PT_ENTRIES] __aligned(PAGE_SIZE);
extern pte_t fixmap_l3[PT_ENTRIES];
extern char _text[], _etext[], __start_rodata[], __end_rodata[], _end[];

static DEFINE_SPINLOCK(kpt_lock);

/* Page-table pages before the buddy allocator is up come from this pool,
 * which is reachable through the kernel image mapping. */
#define EARLY_PT_PAGES 64
static u8 early_pt_pool[EARLY_PT_PAGES][PAGE_SIZE] __aligned(PAGE_SIZE);
static int early_pt_used;
static bool page_alloc_ready;

static pte_t *pt_alloc(void) {
  if (page_alloc_ready) {
    void *p = get_free_page(GFP_ZERO);
    if (!p) panic("mmu: out of memory for page tables");
    return p;
  }
  if (early_pt_used == EARLY_PT_PAGES) panic("mmu: early page-table pool exhausted");
  pte_t *t = (pte_t *)early_pt_pool[early_pt_used++];
  memset(t, 0, PAGE_SIZE);
  return t;
}

/* Table pointer from a table descriptor, valid for kimage-pool and linear pages. */
static pte_t *table_va(pte_t desc) {
  phys_addr_t pa = pte_addr(desc);
  phys_addr_t pool = virt_to_phys(early_pt_pool);
  if (pa >= pool && pa < pool + sizeof(early_pt_pool)) return (pte_t *)((u64)pa + kimage_voffset);
  if (pa >= kernel_phys_start && pa < kernel_phys_end) return (pte_t *)((u64)pa + kimage_voffset);
  return phys_to_virt(pa);
}

/* Replace a block descriptor at `level` (1 or 2) by an equivalent table. */
static void split_block(pte_t *entry, int level) {
  pte_t blk = *entry;
  pte_t *t = pt_alloc();
  u64 step = level == 1 ? L2_SIZE : PAGE_SIZE;
  pte_t attrs = blk & ~PTE_ADDR_MASK;
  if (level == 2) attrs |= PTE_PAGE;
  for (int i = 0; i < PT_ENTRIES; i++) t[i] = (pte_addr(blk) + i * step) | attrs;
  dsb(ishst);
  *entry = virt_to_phys(t) | PTE_TABLE | PTE_VALID;
  flush_tlb_all();
}

/* Return a pointer to the descriptor for va at `level`, allocating tables. */
static pte_t *walk(pte_t *l1, u64 va, int level, bool alloc) {
  pte_t *t = l1;
  for (int lvl = 1; lvl < level; lvl++) {
    int shift = lvl == 1 ? L1_SHIFT : L2_SHIFT;
    pte_t *e = &t[(va >> shift) & (PT_ENTRIES - 1)];
    if (!pte_valid(*e)) {
      if (!alloc) return NULL;
      pte_t *nt = pt_alloc();
      dsb(ishst);
      *e = virt_to_phys(nt) | PTE_TABLE | PTE_VALID;
    } else if (pte_is_block(*e)) {
      if (!alloc) return e;
      split_block(e, lvl);
    }
    t = table_va(*e);
  }
  int shift = level == 1 ? L1_SHIFT : level == 2 ? L2_SHIFT : L3_SHIFT;
  return &t[(va >> shift) & (PT_ENTRIES - 1)];
}

/* Map [va, va+size) -> pa using the largest possible blocks. */
static void map_range(u64 va, phys_addr_t pa, u64 size, u64 page_prot) {
  u64 block_prot = (page_prot & ~PTE_PAGE);
  while (size) {
    if (IS_ALIGNED(va | pa, L1_SIZE) && size >= L1_SIZE) {
      *walk(swapper_pg_dir, va, 1, true) = pa | block_prot;
      va += L1_SIZE, pa += L1_SIZE, size -= L1_SIZE;
    } else if (IS_ALIGNED(va | pa, L2_SIZE) && size >= L2_SIZE) {
      *walk(swapper_pg_dir, va, 2, true) = pa | block_prot;
      va += L2_SIZE, pa += L2_SIZE, size -= L2_SIZE;
    } else {
      *walk(swapper_pg_dir, va, 3, true) = pa | page_prot;
      va += PAGE_SIZE, pa += PAGE_SIZE, size -= PAGE_SIZE;
    }
  }
}

int map_kernel_page(u64 va, phys_addr_t pa, u64 prot) {
  unsigned long f = spin_lock_irqsave(&kpt_lock);
  pte_t *e = walk(swapper_pg_dir, va, 3, true);
  *e = (pa & PTE_ADDR_MASK) | prot;
  dsb(ishst);
  isb();
  spin_unlock_irqrestore(&kpt_lock, f);
  return 0;
}

void unmap_kernel_page(u64 va) {
  unsigned long f = spin_lock_irqsave(&kpt_lock);
  pte_t *e = walk(swapper_pg_dir, va, 3, false);
  if (e && pte_valid(*e) && !pte_is_block(*e)) *e = 0;
  spin_unlock_irqrestore(&kpt_lock, f);
  flush_tlb_kernel_page(va);
}

phys_addr_t kernel_va_to_phys(u64 va) {
  pte_t *t = swapper_pg_dir;
  for (int lvl = 1; lvl <= 3; lvl++) {
    int shift = lvl == 1 ? L1_SHIFT : lvl == 2 ? L2_SHIFT : L3_SHIFT;
    pte_t e = t[(va >> shift) & (PT_ENTRIES - 1)];
    if (!pte_valid(e)) return 0;
    if (lvl == 3 || pte_is_block(e)) return pte_addr(e) + (va & ((1UL << shift) - 1));
    t = table_va(e);
  }
  return 0;
}

static int fixmap_next;

void *early_fixmap(phys_addr_t pa, u64 prot) {
  if (fixmap_next == FIXMAP_NR_PAGES) panic("fixmap exhausted");
  int slot = fixmap_next++;
  u64 va = FIXMAP_START + FIXMAP_PAGES_OFFSET + (u64)slot * PAGE_SIZE;
  fixmap_l3[slot] = (pa & PAGE_MASK) | prot;
  flush_tlb_kernel_page(va);
  return (void *)(va + (pa & ~PAGE_MASK));
}

/* Set the permissions of the kernel image pages in [start, end). */
static void protect_kimage(u64 start, u64 end, u64 prot) {
  for (u64 va = start; va < end; va += PAGE_SIZE) {
    pte_t *e = walk(swapper_pg_dir, va, 3, false);
    if (e && pte_valid(*e)) *e = pte_addr(*e) | prot;
  }
}

void mmu_init(void) {
  /* 1. Linear map of every RAM region. */
  for (int i = 0; i < memblock_memory_count(); i++) {
    const struct memblock_region *r = memblock_memory(i);
    if (r->base + r->size > LINEAR_MAP_SIZE) panic("RAM above the linear map limit");
    map_range(PAGE_OFFSET + r->base, r->base, r->size, PROT_KERNEL_RW);
  }
  /* 2. W^X for the kernel image. */
  protect_kimage((u64)_text, (u64)_etext, PROT_KERNEL_RX);
  protect_kimage((u64)__start_rodata, (u64)__end_rodata, PROT_KERNEL_RO);
  protect_kimage((u64)__end_rodata, ALIGN_UP((u64)_end, PAGE_SIZE), PROT_KERNEL_RW);
  /* 3. Drop the boot identity map from TTBR0. */
  write_sysreg(ttbr0_el1, virt_to_phys(empty_zero_pg_dir));
  isb();
  flush_tlb_all();
}

void mmu_page_alloc_ready(void);
void mmu_page_alloc_ready(void) { page_alloc_ready = true; }

/* ---- cache maintenance ---- */
static u64 dcache_line(void) {
  u64 ctr = read_sysreg(ctr_el0);
  return 4UL << ((ctr >> 16) & 0xf);
}

#define DC_RANGE(op)                                                   \
  u64 line = dcache_line();                                            \
  u64 a = ALIGN_DOWN((u64)va, line), e = (u64)va + size;               \
  for (; a < e; a += line) __asm__ volatile("dc " #op ", %0" ::"r"(a) : "memory"); \
  dsb(sy)

void dcache_clean_range(const void *va, size_t size) { DC_RANGE(cvac); }
void dcache_inval_range(const void *va, size_t size) { DC_RANGE(civac); }
void dcache_flush_range(const void *va, size_t size) { DC_RANGE(civac); }

void icache_inval_all(void) {
  __asm__ volatile("ic ialluis\n dsb ish\n isb" ::: "memory");
}

void sync_icache_range(const void *va, size_t size) {
  u64 line = dcache_line();
  u64 a = ALIGN_DOWN((u64)va, line), e = (u64)va + size;
  for (; a < e; a += line) __asm__ volatile("dc cvau, %0" ::"r"(a) : "memory");
  dsb(ish);
  icache_inval_all();
}
