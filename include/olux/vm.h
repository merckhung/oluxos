#ifndef OLUX_VM_H
#define OLUX_VM_H

#include <asm/pgtable.h>
#include <olux/list.h>
#include <olux/spinlock.h>
#include <olux/types.h>

/* mmap(2) ABI */
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_TYPE 0x0f
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_GROWSDOWN 0x0100
#define MAP_DENYWRITE 0x0800
#define MAP_EXECUTABLE 0x1000
#define MAP_LOCKED 0x2000
#define MAP_NORESERVE 0x4000
#define MAP_POPULATE 0x8000
#define MAP_NONBLOCK 0x10000
#define MAP_STACK 0x20000
#define MAP_FIXED_NOREPLACE 0x100000
#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2

/* vma->flags */
#define VM_READ 0x1
#define VM_WRITE 0x2
#define VM_EXEC 0x4
#define VM_SHARED 0x8
#define VM_IO 0x10        /* device memory: never refcounted / freed */
#define VM_GROWSDOWN 0x20
#define VM_NC 0x40        /* VM_IO mapped Normal-NC (framebuffers) */

struct file;

struct vma {
  u64 start, end; /* [start, end), page aligned */
  u32 flags;
  struct file *file;   /* private file mapping (demand-read), or NULL */
  u64 file_off;        /* file offset corresponding to `start` */
  u64 file_bytes;      /* bytes of file data from `start`; rest is zero */
  phys_addr_t io_base; /* VM_IO: physical address of `start` */
  struct list_head link;
};

struct mm {
  atomic_t refcount;
  pte_t *pgd;
  phys_addr_t pgd_pa;
  u16 asid;
  struct list_head vmas; /* sorted by start */
  u64 brk_start, brk;
  u64 mmap_base;         /* top-down allocation limit */
  u64 stack_top, stack_limit;
  u64 arg_start, arg_end, env_start, env_end;
  u64 entry;
  u64 rss_pages;
  u64 sigtramp;          /* fallback signal return trampoline */
};

#define USER_STACK_MAX (8UL << 20)
#define USER_MMAP_GAP (256UL << 20)
#define USER_EXEC_BASE_PIE 0x0000005500000000UL
#define USER_STACK_TOP_BASE 0x0000007ff0000000UL
#define USER_MIN_ADDR 0x10000UL

struct mm *mm_create(void);
void mm_get(struct mm *mm);
void mm_put(struct mm *mm);
struct mm *mm_dup(struct mm *parent);
struct vma *vma_find(struct mm *mm, u64 addr); /* vma containing addr */
struct vma *vma_find_intersect(struct mm *mm, u64 start, u64 end);
long do_mmap(struct mm *mm, u64 addr, u64 len, int prot, int flags, struct file *file, u64 off);
int do_munmap(struct mm *mm, u64 addr, u64 len);
int do_mprotect(struct mm *mm, u64 addr, u64 len, int prot);
long do_mremap(struct mm *mm, u64 old, u64 old_len, u64 new_len, int flags, u64 new_addr);
long do_brk(struct mm *mm, u64 new_brk);
int map_vma(struct mm *mm, u64 start, u64 end, u32 flags, struct file *file, u64 off, u64 file_bytes);
int map_io(struct mm *mm, u64 start, u64 len, phys_addr_t pa, u32 flags);

/* Page-fault entry. Returns 0 if resolved, -EFAULT (no mapping) or
 * -EACCES (permission) or -ENOMEM. */
int handle_mm_fault(struct mm *mm, u64 addr, bool write, bool exec);
/* Make [addr, addr+len) present (for kernel writes during exec/signals). */
int mm_populate(struct mm *mm, u64 addr, u64 len, bool write);
/* Kernel pointer to the page backing addr (faulting it in), or NULL. */
void *mm_user_page(struct mm *mm, u64 addr, bool write);

u64 mm_get_unmapped_area(struct mm *mm, u64 len);

/* ASIDs / arch */
int asid_alloc(void);
void asid_free(u16 asid);

#endif
