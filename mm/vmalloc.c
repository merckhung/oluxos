/*
 * Kernel virtual area allocator for [VMALLOC_START, VMALLOC_END): vmalloc,
 * ioremap and kernel stacks (each with an unmapped guard page below it so a
 * stack overflow faults instead of silently corrupting memory).
 */
#include <asm/pgtable.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/spinlock.h>

struct vm_area {
  u64 start, size; /* size includes the guard page */
  bool owns_pages;
  struct vm_area *next;
};

static struct vm_area *areas; /* sorted by start */
static DEFINE_SPINLOCK(vm_lock);

static u64 va_alloc(u64 size, bool owns) {
  struct vm_area *a = kmalloc(sizeof(*a), 0);
  if (!a) return 0;
  unsigned long f = spin_lock_irqsave(&vm_lock);
  u64 cand = VMALLOC_START;
  struct vm_area **pp = &areas;
  for (; *pp; pp = &(*pp)->next) {
    if (cand + size <= (*pp)->start) break;
    cand = (*pp)->start + (*pp)->size;
  }
  if (cand + size > VMALLOC_END) {
    spin_unlock_irqrestore(&vm_lock, f);
    kfree(a);
    return 0;
  }
  a->start = cand;
  a->size = size;
  a->owns_pages = owns;
  a->next = *pp;
  *pp = a;
  spin_unlock_irqrestore(&vm_lock, f);
  return cand;
}

static struct vm_area *va_remove(u64 start) {
  unsigned long f = spin_lock_irqsave(&vm_lock);
  for (struct vm_area **pp = &areas; *pp; pp = &(*pp)->next) {
    if ((*pp)->start == start) {
      struct vm_area *a = *pp;
      *pp = a->next;
      spin_unlock_irqrestore(&vm_lock, f);
      return a;
    }
  }
  spin_unlock_irqrestore(&vm_lock, f);
  return NULL;
}

/* Layout of every area: [guard page][mapped pages...] */
static void *vmap_pages(size_t size, u64 prot) {
  size = ALIGN_UP(size, PAGE_SIZE);
  u64 base = va_alloc(size + PAGE_SIZE, true);
  if (!base) return NULL;
  u64 va = base + PAGE_SIZE;
  for (u64 off = 0; off < size; off += PAGE_SIZE) {
    struct page *p = alloc_page(GFP_ZERO);
    if (!p) {
      for (u64 o = 0; o < off; o += PAGE_SIZE) {
        phys_addr_t pa = kernel_va_to_phys(va + o);
        unmap_kernel_page(va + o);
        free_pages(phys_to_page(pa), 0);
      }
      kfree(va_remove(base));
      return NULL;
    }
    map_kernel_page(va + off, page_to_phys(p), prot);
  }
  return (void *)va;
}

void *vmalloc(size_t size) { return vmap_pages(size, PROT_KERNEL_RW); }

void vfree(void *ptr) {
  if (!ptr) return;
  u64 base = (u64)ptr - PAGE_SIZE;
  struct vm_area *a = va_remove(base);
  if (!a) panic("vfree: bad pointer %p", ptr);
  for (u64 va = base + PAGE_SIZE; va < a->start + a->size; va += PAGE_SIZE) {
    phys_addr_t pa = kernel_va_to_phys(va);
    unmap_kernel_page(va);
    if (a->owns_pages && pa) free_pages(phys_to_page(pa), 0);
  }
  kfree(a);
}

void *vmap_stack(size_t size) {
  u8 *base = vmap_pages(size, PROT_KERNEL_RW);
  return base ? base + ALIGN_UP(size, PAGE_SIZE) : NULL; /* returns stack top */
}

void vfree_stack(void *top, size_t size) {
  if (top) vfree((u8 *)top - ALIGN_UP(size, PAGE_SIZE));
}

void *ioremap_prot(phys_addr_t pa, size_t size, u64 prot) {
  phys_addr_t base_pa = ALIGN_DOWN(pa, PAGE_SIZE);
  size_t len = ALIGN_UP(pa + size, PAGE_SIZE) - base_pa;
  u64 base = va_alloc(len + PAGE_SIZE, false);
  if (!base) return NULL;
  for (u64 off = 0; off < len; off += PAGE_SIZE)
    map_kernel_page(base + PAGE_SIZE + off, base_pa + off, prot);
  return (void *)(base + PAGE_SIZE + (pa - base_pa));
}

void *ioremap(phys_addr_t pa, size_t size) { return ioremap_prot(pa, size, PROT_DEVICE); }

void iounmap(void *va) {
  if (va) vfree((void *)ALIGN_DOWN((u64)va, PAGE_SIZE));
}

/* Non-cacheable alias of physically contiguous memory for DMA descriptors. */
void *dma_alloc_coherent(size_t size, phys_addr_t *dma, unsigned gfp) {
  unsigned order = 0;
  while ((PAGE_SIZE << order) < size) order++;
  struct page *p = alloc_pages(gfp | GFP_ZERO, order);
  if (!p) return NULL;
  void *lin = page_address(p);
  dcache_flush_range(lin, PAGE_SIZE << order);
  void *va = ioremap_prot(page_to_phys(p), PAGE_SIZE << order, PROT_NORMAL_NC);
  if (!va) {
    free_pages(p, order);
    return NULL;
  }
  *dma = page_to_phys(p);
  return va;
}

void dma_free_coherent(void *va, size_t size) {
  if (!va) return;
  unsigned order = 0;
  while ((PAGE_SIZE << order) < size) order++;
  phys_addr_t pa = kernel_va_to_phys((u64)va);
  iounmap(va);
  free_pages(phys_to_page(pa), order);
}
