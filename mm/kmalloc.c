/*
 * kmalloc: power-of-two slab caches (16 B .. 2 KiB) carved from single
 * pages, plus whole-page allocations for larger objects. Each slab page
 * keeps its own free list; completely free pages are returned to the buddy
 * allocator. Freed objects are poisoned and double frees are detected.
 */
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/spinlock.h>

#define NR_CLASSES 8 /* 16 .. 2048 */
#define MIN_SHIFT 4
#define FREE_MAGIC 0xf4eef4eef4eef4eeUL

struct slab_class {
  size_t size;
  struct list_head partial; /* pages with at least one free object */
  u64 pages;
};

static struct slab_class classes[NR_CLASSES];
static DEFINE_SPINLOCK(kmalloc_lock);
static u64 bytes_in_use;

void kmalloc_init(void) {
  for (int i = 0; i < NR_CLASSES; i++) {
    classes[i].size = 1UL << (i + MIN_SHIFT);
    list_init(&classes[i].partial);
  }
}

static int size_class(size_t size) {
  for (int i = 0; i < NR_CLASSES; i++)
    if (size <= classes[i].size) return i;
  return -1;
}

static struct page *new_slab(int ci) {
  struct page *p = alloc_page(0);
  if (!p) return NULL;
  p->flags |= PG_SLAB;
  p->slab_class = ci;
  p->slab_inuse = 0;
  size_t sz = classes[ci].size;
  u8 *base = page_address(p);
  void *head = NULL;
  for (size_t off = PAGE_SIZE - sz; (long)off >= 0; off -= sz) {
    u64 *obj = (u64 *)(base + off);
    obj[0] = (u64)head;
    if (sz >= 16) obj[1] = FREE_MAGIC;
    head = obj;
    if (off == 0) break;
  }
  p->freelist = head;
  list_add(&p->lru, &classes[ci].partial);
  classes[ci].pages++;
  return p;
}

void *kmalloc(size_t size, unsigned gfp) {
  if (size == 0) return NULL;
  int ci = size_class(size);
  if (ci < 0) {
    unsigned order = 0;
    while ((PAGE_SIZE << order) < size) order++;
    struct page *p = alloc_pages(gfp & ~GFP_ZERO, order);
    if (!p) return NULL;
    p->flags |= PG_COMPOUND;
    p->order = order;
    void *va = page_address(p);
    if (gfp & GFP_ZERO) memset(va, 0, size);
    unsigned long f = spin_lock_irqsave(&kmalloc_lock);
    bytes_in_use += PAGE_SIZE << order;
    spin_unlock_irqrestore(&kmalloc_lock, f);
    return va;
  }
  unsigned long f = spin_lock_irqsave(&kmalloc_lock);
  struct slab_class *c = &classes[ci];
  struct page *p;
  if (list_empty(&c->partial)) {
    p = new_slab(ci);
    if (!p) {
      spin_unlock_irqrestore(&kmalloc_lock, f);
      return NULL;
    }
  } else {
    p = list_first_entry(&c->partial, struct page, lru);
  }
  u64 *obj = p->freelist;
  if (c->size >= 16 && obj[1] != FREE_MAGIC)
    panic("kmalloc: corrupted free object %p (size %zu)", obj, c->size);
  p->freelist = (void *)obj[0];
  p->slab_inuse++;
  if (!p->freelist) list_del(&p->lru);
  bytes_in_use += c->size;
  spin_unlock_irqrestore(&kmalloc_lock, f);
  if (gfp & GFP_ZERO) memset(obj, 0, c->size);
  else obj[1] = 0;
  return obj;
}

void kfree(const void *ptr) {
  if (!ptr) return;
  struct page *p = virt_to_page(ptr);
  if (p->flags & PG_COMPOUND) {
    unsigned order = p->order;
    p->flags &= ~PG_COMPOUND;
    unsigned long f = spin_lock_irqsave(&kmalloc_lock);
    bytes_in_use -= PAGE_SIZE << order;
    spin_unlock_irqrestore(&kmalloc_lock, f);
    free_pages(p, order);
    return;
  }
  if (!(p->flags & PG_SLAB)) panic("kfree: %p is not a kmalloc pointer", ptr);
  struct slab_class *c = &classes[p->slab_class];
  u64 *obj = (u64 *)ptr;
  if (((u64)ptr & (c->size - 1)) != 0) panic("kfree: misaligned pointer %p", ptr);
  unsigned long f = spin_lock_irqsave(&kmalloc_lock);
  if (c->size >= 16 && obj[1] == FREE_MAGIC) {
    /* Probable double free: confirm by scanning this page's free list. */
    for (u64 *q = p->freelist; q; q = (u64 *)q[0])
      if (q == obj) panic("kfree: double free of %p", ptr);
  }
#ifdef CONFIG_DEBUG
  memset(obj, 0x6b, c->size);
#endif
  bool was_full = !p->freelist;
  obj[0] = (u64)p->freelist;
  if (c->size >= 16) obj[1] = FREE_MAGIC;
  p->freelist = obj;
  p->slab_inuse--;
  bytes_in_use -= c->size;
  if (was_full) list_add(&p->lru, &c->partial);
  if (p->slab_inuse == 0 && c->pages > 1) {
    list_del(&p->lru);
    c->pages--;
    p->flags &= ~PG_SLAB;
    spin_unlock_irqrestore(&kmalloc_lock, f);
    free_pages(p, 0);
    return;
  }
  spin_unlock_irqrestore(&kmalloc_lock, f);
}

size_t ksize(const void *ptr) {
  struct page *p = virt_to_page(ptr);
  if (p->flags & PG_COMPOUND) return PAGE_SIZE << p->order;
  return classes[p->slab_class].size;
}

void *kzalloc(size_t size, unsigned gfp) { return kmalloc(size, gfp | GFP_ZERO); }

void *kcalloc(size_t n, size_t size, unsigned gfp) {
  if (size && n > (size_t)-1 / size) return NULL;
  return kzalloc(n * size, gfp);
}

char *kstrdup(const char *s, unsigned gfp) {
  size_t n = strlen(s) + 1;
  char *d = kmalloc(n, gfp);
  if (d) memcpy(d, s, n);
  return d;
}

u64 kmalloc_bytes_in_use(void) { return bytes_in_use; }
