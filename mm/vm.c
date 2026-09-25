/*
 * User address spaces: VMAs, demand paging, copy-on-write, mmap family.
 * All user mappings use 4 KiB pages in a 3-level table (39-bit VA) tagged
 * with the address space's ASID. Callers hold the big kernel lock.
 */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/random.h>
#include <olux/vm.h>

static bool page_is_shared(pte_t v);

static u64 pte_prot(u32 flags) {
  u64 p = PROT_USER_BASE;
  if (flags & VM_IO) {
    p &= ~PTE_ATTRINDX_MASK;
    p |= (flags & VM_NC) ? PTE_ATTRINDX(MT_NORMAL_NC) : PTE_ATTRINDX(MT_DEVICE_nGnRE);
  }
  if (!(flags & VM_WRITE)) p |= PTE_RDONLY;
  if (!(flags & VM_EXEC)) p |= PTE_UXN;
  if (flags & VM_WRITE) p |= PTE_SW_WRITE;
  return p;
}

/* ---------------- page tables ---------------- */

static pte_t *pt_next(pte_t *e, bool alloc) {
  if (!pte_valid(*e)) {
    if (!alloc) return NULL;
    void *t = get_free_page(GFP_ZERO);
    if (!t) return NULL;
    dsb(ishst);
    *e = virt_to_phys(t) | PTE_TABLE | PTE_VALID;
  }
  return phys_to_virt(pte_addr(*e));
}

static pte_t *user_pte(struct mm *mm, u64 va, bool alloc) {
  pte_t *l2 = pt_next(&mm->pgd[L1_INDEX(va)], alloc);
  if (!l2) return NULL;
  pte_t *l3 = pt_next(&l2[L2_INDEX(va)], alloc);
  if (!l3) return NULL;
  return &l3[L3_INDEX(va)];
}

static void set_pte(pte_t *e, pte_t v) {
  WRITE_ONCE(*e, v);
  dsb(ishst);
}

static void release_pte(struct mm *mm, pte_t v) {
  if (!pte_valid(v)) return;
  if (!(v & PTE_SW_SHARED)) {
    page_put(phys_to_page(pte_addr(v)));
    mm->rss_pages--;
  }
}

/* Unmap and release pages in [start, end). */
static void zap_range(struct mm *mm, u64 start, u64 end) {
  for (u64 va = start; va < end;) {
    pte_t *l1e = &mm->pgd[L1_INDEX(va)];
    if (!pte_valid(*l1e)) {
      va = ALIGN_DOWN(va, L1_SIZE) + L1_SIZE;
      continue;
    }
    pte_t *l2 = phys_to_virt(pte_addr(*l1e));
    pte_t *l2e = &l2[L2_INDEX(va)];
    if (!pte_valid(*l2e)) {
      va = ALIGN_DOWN(va, L2_SIZE) + L2_SIZE;
      continue;
    }
    pte_t *l3 = phys_to_virt(pte_addr(*l2e));
    pte_t *e = &l3[L3_INDEX(va)];
    if (pte_valid(*e)) {
      pte_t old = *e;
      set_pte(e, 0);
      flush_tlb_user_page(mm->asid, va);
      release_pte(mm, old);
    }
    va += PAGE_SIZE;
  }
}

static void free_tables(struct mm *mm) {
  for (int i = 0; i < PT_ENTRIES; i++) {
    if (!pte_valid(mm->pgd[i])) continue;
    pte_t *l2 = phys_to_virt(pte_addr(mm->pgd[i]));
    for (int j = 0; j < PT_ENTRIES; j++) {
      if (!pte_valid(l2[j])) continue;
      pte_t *l3 = phys_to_virt(pte_addr(l2[j]));
      for (int k = 0; k < PT_ENTRIES; k++) release_pte(mm, l3[k]);
      free_page_va(l3);
    }
    free_page_va(l2);
  }
}

/* ---------------- mm lifecycle ---------------- */

struct mm *mm_create(void) {
  struct mm *mm = kzalloc(sizeof(*mm), 0);
  if (!mm) return NULL;
  mm->pgd = get_free_page(GFP_ZERO);
  int asid = asid_alloc();
  if (!mm->pgd || asid < 0) {
    if (mm->pgd) free_page_va(mm->pgd);
    kfree(mm);
    return NULL;
  }
  mm->asid = asid;
  mm->pgd_pa = virt_to_phys(mm->pgd);
  atomic_set(&mm->refcount, 1);
  list_init(&mm->vmas);
  /* ASLR: randomise stack and mmap bases (page granular, up to 256 MiB). */
  u64 rnd = get_random_u64();
  mm->stack_top = USER_STACK_TOP_BASE - ((rnd & 0xffff) << PAGE_SHIFT);
  mm->mmap_base = mm->stack_top - USER_STACK_MAX - USER_MMAP_GAP - (((rnd >> 16) & 0xffff) << PAGE_SHIFT);
  return mm;
}

void mm_get(struct mm *mm) { atomic_inc(&mm->refcount); }

static void vma_free(struct vma *v) {
  if (v->file) file_put(v->file);
  kfree(v);
}

void mm_put(struct mm *mm) {
  if (!mm || atomic_dec_return(&mm->refcount) > 0) return;
  free_tables(mm);
  struct vma *v, *n;
  list_for_each_entry_safe(v, n, &mm->vmas, link) {
    list_del(&v->link);
    vma_free(v);
  }
  free_page_va(mm->pgd);
  asid_free(mm->asid);
  kfree(mm);
}

/* ---------------- VMAs ---------------- */

struct vma *vma_find(struct mm *mm, u64 addr) {
  struct vma *v;
  list_for_each_entry(v, &mm->vmas, link) {
    if (addr < v->start) return NULL;
    if (addr < v->end) return v;
  }
  return NULL;
}

struct vma *vma_find_intersect(struct mm *mm, u64 start, u64 end) {
  struct vma *v;
  list_for_each_entry(v, &mm->vmas, link) {
    if (v->start >= end) return NULL;
    if (v->end > start) return v;
  }
  return NULL;
}

static void vma_insert(struct mm *mm, struct vma *nv) {
  struct list_head *pos;
  list_for_each(pos, &mm->vmas) {
    if (list_entry(pos, struct vma, link)->start > nv->start) break;
  }
  list_add_tail(&nv->link, pos);
}

static struct vma *vma_clone(struct vma *v) {
  struct vma *n = kmalloc(sizeof(*n), 0);
  if (!n) return NULL;
  *n = *v;
  list_init(&n->link);
  if (n->file) file_get(n->file);
  return n;
}

/* Split v at addr (start < addr < end); returns the upper part. */
static struct vma *vma_split(struct mm *mm, struct vma *v, u64 addr) {
  struct vma *hi = vma_clone(v);
  if (!hi) return NULL;
  u64 delta = addr - v->start;
  hi->start = addr;
  hi->file_off = v->file_off + delta;
  hi->file_bytes = v->file_bytes > delta ? v->file_bytes - delta : 0;
  if (v->flags & VM_IO) hi->io_base = v->io_base + delta;
  v->end = addr;
  v->file_bytes = MIN(v->file_bytes, delta);
  list_add(&hi->link, &v->link);
  return hi;
}

int map_vma(struct mm *mm, u64 start, u64 end, u32 flags, struct file *file, u64 off, u64 file_bytes) {
  struct vma *v = kzalloc(sizeof(*v), 0);
  if (!v) return -ENOMEM;
  v->start = start;
  v->end = end;
  v->flags = flags;
  v->file = file;
  if (file) file_get(file);
  v->file_off = off;
  v->file_bytes = file_bytes;
  vma_insert(mm, v);
  return 0;
}

int map_io(struct mm *mm, u64 start, u64 len, phys_addr_t pa, u32 flags) {
  int r = map_vma(mm, start, start + len, flags | VM_IO, NULL, 0, 0);
  if (r) return r;
  struct vma *v = vma_find(mm, start);
  v->io_base = pa;
  return 0;
}

u64 mm_get_unmapped_area(struct mm *mm, u64 len) {
  len = ALIGN_UP(len, PAGE_SIZE);
  u64 top = mm->mmap_base;
  /* walk VMAs from high to low looking for a hole below `top` */
  struct vma *v;
  for (;;) {
    if (top < len + USER_MIN_ADDR) return 0;
    u64 cand = top - len;
    v = vma_find_intersect(mm, cand, top);
    if (!v) {
      /* also stay clear of brk growth */
      if (cand < mm->brk + USER_MMAP_GAP / 4 && top > mm->brk_start) {
        top = mm->brk_start;
        if (top <= mm->brk) return 0;
        continue;
      }
      return cand;
    }
    top = v->start;
  }
}

/* ---------------- faults ---------------- */

static int fill_page(struct vma *v, u64 va, void *page) {
  u64 delta = va - v->start;
  if (!v->file || delta >= v->file_bytes) return 0;
  size_t n = MIN((u64)PAGE_SIZE, v->file_bytes - delta);
  ssize_t r = kernel_pread(v->file, page, n, v->file_off + delta);
  return r < 0 ? (int)r : 0;
}

static int fault_in(struct mm *mm, struct vma *v, u64 va, bool write) {
  pte_t *e = user_pte(mm, va, true);
  if (!e) return -ENOMEM;
  pte_t old = *e;
  if (!pte_valid(old)) {
    if (v->flags & VM_IO) {
      set_pte(e, (v->io_base + (va - v->start)) | pte_prot(v->flags) | PTE_SW_SHARED);
      return 0;
    }
    struct page *p = alloc_page(GFP_ZERO);
    if (!p) return -ENOMEM;
    int r = fill_page(v, va, page_address(p));
    if (r) {
      free_page(p);
      return r;
    }
    if (v->flags & VM_EXEC) sync_icache_range(page_address(p), PAGE_SIZE);
    set_pte(e, page_to_phys(p) | pte_prot(v->flags));
    mm->rss_pages++;
    return 0;
  }
  if (write && (old & PTE_RDONLY)) {
    if (!(v->flags & VM_WRITE)) return -EACCES;
    struct page *p = phys_to_page(pte_addr(old));
    if (!(old & PTE_SW_SHARED) && __atomic_load_n(&p->refcount, __ATOMIC_ACQUIRE) == 1) {
      /* sole owner: just make it writable again */
      set_pte(e, (old & ~(PTE_RDONLY | PTE_SW_COW)) | PTE_SW_WRITE);
    } else {
      struct page *np = alloc_page(0);
      if (!np) return -ENOMEM;
      memcpy(page_address(np), phys_to_virt(pte_addr(old)), PAGE_SIZE);
      if (v->flags & VM_EXEC) sync_icache_range(page_address(np), PAGE_SIZE);
      set_pte(e, page_to_phys(np) | pte_prot(v->flags));
      if (!(old & PTE_SW_SHARED)) page_put(p);
      else mm->rss_pages++;
    }
    flush_tlb_user_page(mm->asid, va);
    return 0;
  }
  /* Present and permitted: stale TLB entry on this CPU. */
  flush_tlb_user_page(mm->asid, va);
  return 0;
}

int handle_mm_fault(struct mm *mm, u64 addr, bool write, bool exec) {
  if (!mm || addr >= USER_VA_END) return -EFAULT;
  u64 va = ALIGN_DOWN(addr, PAGE_SIZE);
  struct vma *v = vma_find(mm, va);
  if (!v) {
    /* grow the stack downwards */
    struct vma *s = vma_find_intersect(mm, va, USER_VA_END);
    if (!s || !(s->flags & VM_GROWSDOWN) || s->start - va > USER_STACK_MAX ||
        mm->stack_top - va > USER_STACK_MAX)
      return -EFAULT;
    s->start = va;
    v = s;
  }
  if (write && !(v->flags & VM_WRITE)) return -EACCES;
  if (exec && !(v->flags & VM_EXEC)) return -EACCES;
  if (!write && !exec && !(v->flags & (VM_READ | VM_EXEC | VM_WRITE))) return -EACCES;
  return fault_in(mm, v, va, write);
}

int mm_populate(struct mm *mm, u64 addr, u64 len, bool write) {
  for (u64 va = ALIGN_DOWN(addr, PAGE_SIZE); va < addr + len; va += PAGE_SIZE) {
    int r = handle_mm_fault(mm, va, write, false);
    if (r) return r;
  }
  return 0;
}

void *mm_user_page(struct mm *mm, u64 addr, bool write) {
  if (handle_mm_fault(mm, addr, write, false)) return NULL;
  pte_t *e = user_pte(mm, addr, false);
  if (!e || !pte_valid(*e)) return NULL;
  if (write && (*e & PTE_RDONLY)) {
    if (handle_mm_fault(mm, addr, true, false)) return NULL;
  }
  return (u8 *)phys_to_virt(pte_addr(*e)) + (addr & ~PAGE_MASK);
}

/* ---------------- fork ---------------- */

struct mm *mm_dup(struct mm *parent) {
  struct mm *mm = mm_create();
  if (!mm) return NULL;
  mm->brk_start = parent->brk_start;
  mm->brk = parent->brk;
  mm->mmap_base = parent->mmap_base;
  mm->stack_top = parent->stack_top;
  mm->arg_start = parent->arg_start;
  mm->arg_end = parent->arg_end;
  mm->env_start = parent->env_start;
  mm->env_end = parent->env_end;
  mm->entry = parent->entry;
  mm->sigtramp = parent->sigtramp;

  struct vma *v;
  list_for_each_entry(v, &parent->vmas, link) {
    struct vma *nv = vma_clone(v);
    if (!nv) goto fail;
    list_add_tail(&nv->link, &mm->vmas);
    for (u64 va = v->start; va < v->end; va += PAGE_SIZE) {
      pte_t *pe = user_pte(parent, va, false);
      if (!pe || !pte_valid(*pe)) continue;
      pte_t *ce = user_pte(mm, va, true);
      if (!ce) goto fail;
      pte_t pv = *pe;
      if (pv & PTE_SW_SHARED) {
        set_pte(ce, pv);
        continue;
      }
      page_get(phys_to_page(pte_addr(pv)));
      mm->rss_pages++;
      if (!(v->flags & VM_SHARED)) {
        /* private: both sides become read-only copy-on-write */
        pv |= PTE_RDONLY | PTE_SW_COW;
        set_pte(pe, pv);
      }
      set_pte(ce, pv);
    }
  }
  flush_tlb_asid(parent->asid);
  return mm;
fail:
  mm_put(mm);
  return NULL;
}

/* ---------------- mmap family ---------------- */

static u32 prot_to_vm(int prot) {
  u32 f = 0;
  if (prot & PROT_READ) f |= VM_READ;
  if (prot & PROT_WRITE) f |= VM_WRITE | VM_READ;
  if (prot & PROT_EXEC) f |= VM_EXEC | VM_READ;
  return f;
}

/* Remove [start, end) from the VMA list, splitting at the edges. */
static int unmap_region(struct mm *mm, u64 start, u64 end) {
  struct vma *v, *n;
  list_for_each_entry_safe(v, n, &mm->vmas, link) {
    if (v->end <= start) continue;
    if (v->start >= end) break;
    if (v->start < start) {
      if (!vma_split(mm, v, start)) return -ENOMEM;
      n = list_entry(v->link.next, struct vma, link);
      continue;
    }
    if (v->end > end) {
      if (!vma_split(mm, v, end)) return -ENOMEM;
    }
    zap_range(mm, v->start, v->end);
    n = list_entry(v->link.next, struct vma, link);
    list_del(&v->link);
    vma_free(v);
  }
  return 0;
}

long do_mmap(struct mm *mm, u64 addr, u64 len, int prot, int flags, struct file *file, u64 off) {
  if (!len) return -EINVAL;
  if (off & ~PAGE_MASK) return -EINVAL;
  u64 alen = ALIGN_UP(len, PAGE_SIZE);
  if (alen < len || alen > USER_VA_END) return -ENOMEM;
  int type = flags & MAP_TYPE;
  if (type != MAP_SHARED && type != MAP_PRIVATE) return -EINVAL;
  if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) {
    if (addr & ~PAGE_MASK) return -EINVAL;
    if (addr < USER_MIN_ADDR || addr + alen > USER_VA_END) return -ENOMEM;
    if (vma_find_intersect(mm, addr, addr + alen)) {
      if (flags & MAP_FIXED_NOREPLACE) return -EEXIST;
      int r = unmap_region(mm, addr, addr + alen);
      if (r) return r;
    }
  } else {
    addr = ALIGN_DOWN(addr, PAGE_SIZE);
    if (!addr || addr < USER_MIN_ADDR || addr + alen > USER_VA_END ||
        vma_find_intersect(mm, addr, addr + alen))
      addr = mm_get_unmapped_area(mm, alen);
    if (!addr) return -ENOMEM;
  }
  u32 vmf = prot_to_vm(prot);
  if (type == MAP_SHARED) vmf |= VM_SHARED;

  if (file && !(flags & MAP_ANONYMOUS)) {
    if (file->f_op && file->f_op->mmap) {
      /* device mapping (framebuffer, etc.) */
      int r = map_vma(mm, addr, addr + alen, vmf, NULL, 0, 0);
      if (r) return r;
      struct vma *v = vma_find(mm, addr);
      r = file->f_op->mmap(file, v, off);
      if (r) {
        unmap_region(mm, addr, addr + alen);
        return r;
      }
      return addr;
    }
    if (!file->inode || !S_ISREG(file->inode->mode)) return -ENODEV;
    if (type == MAP_SHARED && (prot & PROT_WRITE)) return -EOPNOTSUPP;
    u64 fsize = file->inode->size;
    u64 fbytes = off >= fsize ? 0 : MIN(alen, fsize - off);
    int r = map_vma(mm, addr, addr + alen, vmf & ~VM_SHARED, file, off, fbytes);
    if (r) return r;
  } else {
    int r = map_vma(mm, addr, addr + alen, vmf, NULL, 0, 0);
    if (r) return r;
  }
  if (flags & MAP_POPULATE) mm_populate(mm, addr, alen, prot & PROT_WRITE);
  return addr;
}

int do_munmap(struct mm *mm, u64 addr, u64 len) {
  if ((addr & ~PAGE_MASK) || !len) return -EINVAL;
  u64 end = addr + ALIGN_UP(len, PAGE_SIZE);
  if (end > USER_VA_END || end < addr) return -EINVAL;
  return unmap_region(mm, addr, end);
}

int do_mprotect(struct mm *mm, u64 addr, u64 len, int prot) {
  if (addr & ~PAGE_MASK) return -EINVAL;
  u64 end = addr + ALIGN_UP(len, PAGE_SIZE);
  if (end < addr) return -EINVAL;
  /* the whole range must be mapped */
  for (u64 a = addr; a < end;) {
    struct vma *v = vma_find(mm, a);
    if (!v) return -ENOMEM;
    a = v->end;
  }
  struct vma *v, *n;
  list_for_each_entry_safe(v, n, &mm->vmas, link) {
    if (v->end <= addr) continue;
    if (v->start >= end) break;
    if (v->start < addr) {
      if (!vma_split(mm, v, addr)) return -ENOMEM;
      n = list_entry(v->link.next, struct vma, link);
      continue;
    }
    if (v->end > end && !vma_split(mm, v, end)) return -ENOMEM;
    u32 keep = v->flags & ~(VM_READ | VM_WRITE | VM_EXEC);
    v->flags = keep | prot_to_vm(prot);
    for (u64 va = v->start; va < v->end; va += PAGE_SIZE) {
      pte_t *e = user_pte(mm, va, false);
      if (!e || !pte_valid(*e)) continue;
      pte_t old = *e;
      pte_t nv = (old & PTE_ADDR_MASK) | pte_prot(v->flags) | (old & (PTE_SW_SHARED | PTE_SW_COW));
      if ((old & PTE_SW_COW) || (!(v->flags & VM_SHARED) && page_is_shared(old))) nv |= PTE_RDONLY | PTE_SW_COW;
      if ((v->flags & VM_EXEC) && (old & PTE_UXN)) sync_icache_range(phys_to_virt(pte_addr(old)), PAGE_SIZE);
      set_pte(e, nv);
    }
    n = list_entry(v->link.next, struct vma, link);
  }
  flush_tlb_asid(mm->asid);
  return 0;
}

static bool page_is_shared(pte_t v) {
  if (v & PTE_SW_SHARED) return false;
  return phys_to_page(pte_addr(v))->refcount > 1;
}

long do_mremap(struct mm *mm, u64 old, u64 old_len, u64 new_len, int flags, u64 new_addr) {
  if ((old & ~PAGE_MASK) || !new_len) return -EINVAL;
  old_len = ALIGN_UP(old_len, PAGE_SIZE);
  new_len = ALIGN_UP(new_len, PAGE_SIZE);
  struct vma *v = vma_find(mm, old);
  if (!v || old + old_len > v->end || (v->flags & VM_IO)) return -EFAULT;
  if (flags & MREMAP_FIXED) return -EINVAL;
  if (new_len <= old_len) {
    if (new_len < old_len) do_munmap(mm, old + new_len, old_len - new_len);
    return old;
  }
  /* grow in place if the following range is free */
  if (old + old_len == v->end && !vma_find_intersect(mm, v->end, old + new_len) &&
      old + new_len <= USER_VA_END) {
    v->end = old + new_len;
    return old;
  }
  if (!(flags & MREMAP_MAYMOVE)) return -ENOMEM;
  u64 dst = mm_get_unmapped_area(mm, new_len);
  if (!dst) return -ENOMEM;
  int r = map_vma(mm, dst, dst + new_len, v->flags, v->file, v->file_off + (old - v->start),
                  v->file_bytes > old - v->start ? v->file_bytes - (old - v->start) : 0);
  if (r) return r;
  /* move page table entries */
  for (u64 off = 0; off < old_len; off += PAGE_SIZE) {
    pte_t *se = user_pte(mm, old + off, false);
    if (!se || !pte_valid(*se)) continue;
    pte_t *de = user_pte(mm, dst + off, true);
    if (!de) return -ENOMEM;
    set_pte(de, *se);
    set_pte(se, 0);
    flush_tlb_user_page(mm->asid, old + off);
  }
  /* the source range now has no pages; drop its VMA */
  unmap_region(mm, old, old + old_len);
  return dst;
}

long do_brk(struct mm *mm, u64 nbrk) {
  if (nbrk < mm->brk_start) return mm->brk;
  u64 old_end = ALIGN_UP(mm->brk, PAGE_SIZE);
  u64 new_end = ALIGN_UP(nbrk, PAGE_SIZE);
  if (new_end > old_end) {
    if (vma_find_intersect(mm, old_end, new_end + PAGE_SIZE)) return mm->brk;
    struct vma *v = old_end > mm->brk_start ? vma_find(mm, old_end - PAGE_SIZE) : NULL;
    if (v && v->end == old_end && !v->file && v->flags == (VM_READ | VM_WRITE)) v->end = new_end;
    else if (map_vma(mm, old_end, new_end, VM_READ | VM_WRITE, NULL, 0, 0)) return mm->brk;
  } else if (new_end < old_end) {
    unmap_region(mm, new_end, old_end);
  }
  mm->brk = nbrk;
  return nbrk;
}
