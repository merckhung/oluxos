/*
 * Binary buddy page allocator with three zones:
 *   DMA    [0, 1 GiB)   - reachable by BCM2711 legacy DMA / EMMC2
 *   DMA32  [1, 4 GiB)   - 32-bit DMA masters
 *   NORMAL [4 GiB, ...) - everything else
 * A request falls back from its preferred zone to lower zones.
 */
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/spinlock.h>

#define ZONE_DMA 0
#define ZONE_DMA32 1
#define ZONE_NORMAL 2
#define NR_ZONES 3

struct zone {
  const char *name;
  u64 start_pfn, end_pfn;
  struct list_head free[MAX_ORDER];
  u64 nr_free[MAX_ORDER];
  u64 free_pages, managed_pages;
};

static struct zone zones[NR_ZONES] = {
    {.name = "DMA", .start_pfn = 0, .end_pfn = (1UL << 30) >> PAGE_SHIFT},
    {.name = "DMA32", .start_pfn = (1UL << 30) >> PAGE_SHIFT, .end_pfn = (1UL << 32) >> PAGE_SHIFT},
    {.name = "Normal", .start_pfn = (1UL << 32) >> PAGE_SHIFT, .end_pfn = ~0UL},
};

struct page *mem_map;
u64 mem_map_base_pfn, mem_map_pfns;
static DEFINE_SPINLOCK(zone_lock);

void mmu_page_alloc_ready(void);

static struct zone *pfn_zone(u64 pfn) {
  for (int z = NR_ZONES - 1; z >= 0; z--)
    if (pfn >= zones[z].start_pfn) return &zones[z];
  return &zones[0];
}

static void add_free(struct zone *z, struct page *p, unsigned order) {
  p->flags |= PG_BUDDY;
  p->order = order;
  list_add(&p->lru, &z->free[order]);
  z->nr_free[order]++;
}

static void del_free(struct zone *z, struct page *p, unsigned order) {
  list_del(&p->lru);
  p->flags &= ~PG_BUDDY;
  z->nr_free[order]--;
}

static void __free(struct page *p, unsigned order) {
  u64 pfn = page_to_pfn(p);
  struct zone *z = pfn_zone(pfn);
  z->free_pages += 1UL << order;
  while (order < MAX_ORDER - 1) {
    u64 buddy_pfn = pfn ^ (1UL << order);
    if (!pfn_valid(buddy_pfn) || buddy_pfn < z->start_pfn || buddy_pfn >= z->end_pfn) break;
    struct page *b = pfn_to_page(buddy_pfn);
    if (!(b->flags & PG_BUDDY) || b->order != order) break;
    del_free(z, b, order);
    pfn &= ~(1UL << order);
    order++;
  }
  add_free(z, pfn_to_page(pfn), order);
}

static struct page *__alloc(struct zone *z, unsigned order) {
  for (unsigned o = order; o < MAX_ORDER; o++) {
    if (list_empty(&z->free[o])) continue;
    struct page *p = list_first_entry(&z->free[o], struct page, lru);
    del_free(z, p, o);
    while (o > order) { /* split, returning the upper halves */
      o--;
      add_free(z, p + (1UL << o), o);
    }
    z->free_pages -= 1UL << order;
    return p;
  }
  return NULL;
}

struct page *alloc_pages(unsigned gfp, unsigned order) {
  if (order >= MAX_ORDER) return NULL;
  int top = (gfp & GFP_DMA) ? ZONE_DMA : (gfp & GFP_DMA32) ? ZONE_DMA32 : ZONE_NORMAL;
  struct page *p = NULL;
  unsigned long f = spin_lock_irqsave(&zone_lock);
  for (int zi = top; zi >= 0 && !p; zi--) p = __alloc(&zones[zi], order);
  spin_unlock_irqrestore(&zone_lock, f);
  if (!p) return NULL;
  for (unsigned i = 0; i < (1U << order); i++) {
    if (p[i].flags & (PG_RESERVED | PG_SLAB))
      panic("page_alloc: allocated page %#llx has flags %#x", (unsigned long long)page_to_phys(&p[i]), p[i].flags);
    p[i].refcount = 1;
  }
  p->order = order;
  if (gfp & GFP_ZERO) memset(page_address(p), 0, PAGE_SIZE << order);
  return p;
}

void free_pages(struct page *p, unsigned order) {
  if (!p) return;
  if (p->flags & (PG_BUDDY | PG_RESERVED))
    panic("free_pages: double free or reserved page %#llx", (unsigned long long)page_to_phys(p));
  for (unsigned i = 0; i < (1U << order); i++) p[i].refcount = 0;
#ifdef CONFIG_DEBUG
  memset(page_address(p), 0x6b, PAGE_SIZE << order);
#endif
  unsigned long f = spin_lock_irqsave(&zone_lock);
  __free(p, order);
  spin_unlock_irqrestore(&zone_lock, f);
}

void *get_free_pages(unsigned gfp, unsigned order) {
  struct page *p = alloc_pages(gfp, order);
  return p ? page_address(p) : NULL;
}

void free_pages_va(void *va, unsigned order) {
  if (va) free_pages(virt_to_page(va), order);
}

void page_get(struct page *p) { __atomic_add_fetch(&p->refcount, 1, __ATOMIC_RELAXED); }

bool page_put(struct page *p) {
  s32 r = __atomic_sub_fetch(&p->refcount, 1, __ATOMIC_ACQ_REL);
  if (r < 0) panic("page_put: refcount underflow on %#llx", (unsigned long long)page_to_phys(p));
  if (r == 0) {
    p->refcount = 1; /* free_pages expects an allocated page */
    free_pages(p, 0);
    return true;
  }
  return false;
}

u64 nr_free_pages(void) {
  u64 n = 0;
  for (int i = 0; i < NR_ZONES; i++) n += zones[i].free_pages;
  return n;
}

u64 nr_total_pages(void) {
  u64 n = 0;
  for (int i = 0; i < NR_ZONES; i++) n += zones[i].managed_pages;
  return n;
}

void page_alloc_init(void) {
  for (int z = 0; z < NR_ZONES; z++)
    for (int o = 0; o < MAX_ORDER; o++) list_init(&zones[z].free[o]);

  u64 start = memblock_start() >> PAGE_SHIFT;
  u64 end = memblock_end() >> PAGE_SHIFT;
  mem_map_base_pfn = start;
  mem_map_pfns = end - start;
  size_t bytes = ALIGN_UP(mem_map_pfns * sizeof(struct page), PAGE_SIZE);
  phys_addr_t pa = memblock_alloc(bytes, PAGE_SIZE, ~0UL);
  if (!pa) panic("page_alloc: cannot allocate mem_map (%zu bytes)", bytes);
  mem_map = phys_to_virt(pa);
  memset(mem_map, 0, bytes);
  for (u64 i = 0; i < mem_map_pfns; i++) {
    mem_map[i].flags = PG_RESERVED;
    list_init(&mem_map[i].lru);
  }

  /* Hand every non-reserved RAM page to the buddy allocator. */
  for (int i = 0; i < memblock_memory_count(); i++) {
    const struct memblock_region *r = memblock_memory(i);
    for (phys_addr_t a = r->base; a < r->base + r->size; a += PAGE_SIZE) {
      if (memblock_is_reserved(a)) continue;
      struct page *p = phys_to_page(a);
      p->flags = 0;
      pfn_zone(a >> PAGE_SHIFT)->managed_pages++;
      __free(p, 0);
    }
  }
  mmu_page_alloc_ready();
  for (int z = 0; z < NR_ZONES; z++)
    if (zones[z].managed_pages)
      pr_info("zone %-6s: %llu pages free\n", zones[z].name, (unsigned long long)zones[z].free_pages);
}
