#ifndef OLUX_MM_H
#define OLUX_MM_H

#include <asm/memory.h>
#include <olux/list.h>
#include <olux/spinlock.h>
#include <olux/types.h>

/* ---- early boot allocator (memblock) ---- */
#define MEMBLOCK_MAX 64
struct memblock_region {
  phys_addr_t base, size;
};
void memblock_add(phys_addr_t base, phys_addr_t size);
void memblock_reserve(phys_addr_t base, phys_addr_t size);
phys_addr_t memblock_alloc(phys_addr_t size, phys_addr_t align, phys_addr_t limit);
phys_addr_t memblock_start(void);
phys_addr_t memblock_end(void);
int memblock_memory_count(void);
const struct memblock_region *memblock_memory(int i);
bool memblock_is_reserved(phys_addr_t pa);
void memblock_dump(void);
phys_addr_t memblock_total(void);

/* ---- page allocator ---- */
#define GFP_KERNEL 0x0
#define GFP_ZERO 0x1
#define GFP_DMA 0x2   /* below 1 GiB (BCM2711 legacy DMA) */
#define GFP_DMA32 0x4 /* below 4 GiB */
#define GFP_ATOMIC 0x8 /* never sleeps (all allocations are atomic today) */

#define MAX_ORDER 11 /* up to 4 MiB contiguous */

#define PG_RESERVED 0x1
#define PG_BUDDY 0x2 /* free, head of a buddy block */
#define PG_SLAB 0x4
#define PG_COMPOUND 0x8

struct page {
  u32 flags;
  s32 refcount;
  u16 order;       /* buddy order (free) or allocation order */
  u16 slab_class;  /* kmalloc size class for PG_SLAB pages */
  u32 slab_inuse;
  void *freelist; /* slab: first free object in this page */
  struct list_head lru;
};

extern struct page *mem_map;
extern u64 mem_map_base_pfn, mem_map_pfns;

static inline u64 page_to_pfn(const struct page *p) { return (p - mem_map) + mem_map_base_pfn; }
static inline struct page *pfn_to_page(u64 pfn) { return &mem_map[pfn - mem_map_base_pfn]; }
static inline phys_addr_t page_to_phys(const struct page *p) { return page_to_pfn(p) << PAGE_SHIFT; }
static inline struct page *phys_to_page(phys_addr_t pa) { return pfn_to_page(pa >> PAGE_SHIFT); }
static inline void *page_address(const struct page *p) { return phys_to_virt(page_to_phys(p)); }
static inline struct page *virt_to_page(const void *va) { return phys_to_page(virt_to_phys(va)); }
static inline bool pfn_valid(u64 pfn) { return pfn >= mem_map_base_pfn && pfn < mem_map_base_pfn + mem_map_pfns; }

void page_alloc_init(void);
struct page *alloc_pages(unsigned gfp, unsigned order);
void free_pages(struct page *p, unsigned order);
static inline struct page *alloc_page(unsigned gfp) { return alloc_pages(gfp, 0); }
static inline void free_page(struct page *p) { free_pages(p, 0); }
void *get_free_pages(unsigned gfp, unsigned order); /* linear-map VA */
void free_pages_va(void *va, unsigned order);
static inline void *get_free_page(unsigned gfp) { return get_free_pages(gfp, 0); }
static inline void free_page_va(void *va) { free_pages_va(va, 0); }
void page_get(struct page *p);
bool page_put(struct page *p); /* returns true if the page was freed */
u64 nr_free_pages(void);
u64 nr_total_pages(void);

/* ---- kmalloc ---- */
void kmalloc_init(void);
void *kmalloc(size_t size, unsigned gfp);
void *kzalloc(size_t size, unsigned gfp);
void *kcalloc(size_t n, size_t size, unsigned gfp);
void kfree(const void *p);
char *kstrdup(const char *s, unsigned gfp);
size_t ksize(const void *p);
u64 kmalloc_bytes_in_use(void);

/* ---- kernel virtual mappings ---- */
void mmu_init(void);                  /* linear map, remap kernel W^X */
void *early_fixmap(phys_addr_t pa, u64 prot); /* one page, before vmalloc */
void *ioremap(phys_addr_t pa, size_t size);
void *ioremap_prot(phys_addr_t pa, size_t size, u64 prot);
void iounmap(void *va);
void *vmalloc(size_t size);
void vfree(void *va);
void *vmap_stack(size_t size); /* guard page below */
void vfree_stack(void *top, size_t size);
int map_kernel_page(u64 va, phys_addr_t pa, u64 prot);
void unmap_kernel_page(u64 va);
phys_addr_t kernel_va_to_phys(u64 va);

/* ---- cache maintenance (for DMA / firmware buffers) ---- */
void dcache_clean_range(const void *va, size_t size);
void dcache_inval_range(const void *va, size_t size);
void dcache_flush_range(const void *va, size_t size);
void icache_inval_all(void);
void sync_icache_range(const void *va, size_t size);

/* ---- DMA-coherent memory (non-cacheable mapping) ---- */
void *dma_alloc_coherent(size_t size, phys_addr_t *dma, unsigned gfp);
void dma_free_coherent(void *va, size_t size);

#endif
