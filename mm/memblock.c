/*
 * Early physical memory bookkeeping: the list of RAM regions (from the
 * device tree) and the list of reserved regions (kernel image, DTB, initrd,
 * firmware carve-outs). Used to bring up the linear map and the buddy
 * allocator, then only consulted read-only.
 */
#include <olux/kernel.h>
#include <olux/mm.h>

struct region_list {
  int n;
  struct memblock_region r[MEMBLOCK_MAX];
};

static struct region_list memory, reserved;

static void insert(struct region_list *l, phys_addr_t base, phys_addr_t size) {
  if (!size) return;
  phys_addr_t end = base + size;
  /* merge with overlapping/adjacent regions, keep sorted */
  int i = 0;
  while (i < l->n) {
    struct memblock_region *r = &l->r[i];
    if (r->base + r->size < base || end < r->base) {
      i++;
      continue;
    }
    base = MIN(base, r->base);
    end = MAX(end, r->base + r->size);
    memmove(r, r + 1, (l->n - i - 1) * sizeof(*r));
    l->n--;
  }
  if (l->n == MEMBLOCK_MAX) panic("memblock: too many regions");
  for (i = 0; i < l->n && l->r[i].base < base; i++)
    ;
  memmove(&l->r[i + 1], &l->r[i], (l->n - i) * sizeof(l->r[0]));
  l->r[i].base = base;
  l->r[i].size = end - base;
  l->n++;
}

void memblock_add(phys_addr_t base, phys_addr_t size) {
  phys_addr_t b = ALIGN_UP(base, PAGE_SIZE);
  phys_addr_t e = ALIGN_DOWN(base + size, PAGE_SIZE);
  if (e > b) insert(&memory, b, e - b);
}

void memblock_reserve(phys_addr_t base, phys_addr_t size) {
  phys_addr_t b = ALIGN_DOWN(base, PAGE_SIZE);
  phys_addr_t e = ALIGN_UP(base + size, PAGE_SIZE);
  insert(&reserved, b, e - b);
}

bool memblock_is_reserved(phys_addr_t pa) {
  for (int i = 0; i < reserved.n; i++)
    if (pa >= reserved.r[i].base && pa - reserved.r[i].base < reserved.r[i].size) return true;
  return false;
}

/* Top-down allocation below `limit` avoiding reserved ranges. */
phys_addr_t memblock_alloc(phys_addr_t size, phys_addr_t align, phys_addr_t limit) {
  size = ALIGN_UP(size, PAGE_SIZE);
  for (int i = memory.n - 1; i >= 0; i--) {
    phys_addr_t lo = memory.r[i].base;
    phys_addr_t hi = MIN(memory.r[i].base + memory.r[i].size, limit);
    if (hi <= lo || hi - lo < size) continue;
    phys_addr_t cand = ALIGN_DOWN(hi - size, align);
    while (cand >= lo) {
      phys_addr_t conflict_base = 0;
      bool ok = true;
      for (int j = 0; j < reserved.n; j++) {
        struct memblock_region *r = &reserved.r[j];
        if (cand < r->base + r->size && r->base < cand + size) {
          ok = false;
          conflict_base = r->base;
          break;
        }
      }
      if (ok) {
        memblock_reserve(cand, size);
        return cand;
      }
      if (conflict_base < size) break;
      cand = ALIGN_DOWN(conflict_base - size, align);
    }
  }
  return 0;
}

phys_addr_t memblock_start(void) { return memory.n ? memory.r[0].base : 0; }
phys_addr_t memblock_end(void) {
  return memory.n ? memory.r[memory.n - 1].base + memory.r[memory.n - 1].size : 0;
}
int memblock_memory_count(void) { return memory.n; }
const struct memblock_region *memblock_memory(int i) { return &memory.r[i]; }

phys_addr_t memblock_total(void) {
  phys_addr_t t = 0;
  for (int i = 0; i < memory.n; i++) t += memory.r[i].size;
  return t;
}

void memblock_dump(void) {
  for (int i = 0; i < memory.n; i++)
    pr_info("  memory:   [0x%012llx-0x%012llx] %llu MiB\n", (unsigned long long)memory.r[i].base,
            (unsigned long long)(memory.r[i].base + memory.r[i].size - 1),
            (unsigned long long)(memory.r[i].size >> 20));
  for (int i = 0; i < reserved.n; i++)
    pr_info("  reserved: [0x%012llx-0x%012llx]\n", (unsigned long long)reserved.r[i].base,
            (unsigned long long)(reserved.r[i].base + reserved.r[i].size - 1));
}
