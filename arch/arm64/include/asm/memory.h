#ifndef ASM_MEMORY_H
#define ASM_MEMORY_H

#include <olux/const.h>

/*
 * Virtual memory layout (4 KiB granule, 39-bit VA, 3-level tables).
 *
 *   0x0000000000000000 - 0x0000007fffffffff  user (TTBR0, per process)
 *   0xffffff8000000000 - 0xffffffbfffffffff  linear map of all RAM (256 GiB)
 *   0xffffffc000000000 - 0xfffffffeffffffff  vmalloc: kernel stacks, ioremap
 *   0xffffffff40000000 - 0xffffffff7fffffff  fixmap: early DTB / console
 *   0xffffffff80000000 - 0xffffffffbfffffff  kernel image
 */
#define VA_BITS 39
#define PAGE_SHIFT 12
#define PAGE_SIZE (UL(1) << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define PAGE_OFFSET UL(0xffffff8000000000)
#define LINEAR_MAP_SIZE UL(0x0000004000000000)
#define VMALLOC_START UL(0xffffffc000000000)
#define VMALLOC_END UL(0xffffffff00000000)
#define FIXMAP_START UL(0xffffffff40000000)
#define KIMAGE_VADDR UL(0xffffffff80000000)

#define USER_VA_END (UL(1) << VA_BITS)

/* Early boot maps the kernel image with 4 KiB pages using this many L3 tables. */
#define KIMAGE_MAX_SIZE (UL(32) << 20)
#define KIMAGE_L3_TABLES (KIMAGE_MAX_SIZE >> 21)

/* Fixmap layout (offsets from FIXMAP_START). */
#define FIXMAP_DTB_OFFSET UL(0) /* 4 MiB window, two 2 MiB blocks */
#define FIXMAP_DTB_SIZE (UL(4) << 20)
#define FIXMAP_PAGES_OFFSET (UL(4) << 20) /* 4 KiB fixmap slots */
#define FIXMAP_NR_PAGES 512

#define THREAD_STACK_SIZE (UL(16) * 1024)
#define BOOT_STACK_SIZE (UL(16) * 1024)

#ifndef __ASSEMBLY__
#include <olux/types.h>

extern u64 kimage_voffset; /* KIMAGE VA - PA */
extern phys_addr_t kernel_phys_start, kernel_phys_end;

static inline void *phys_to_virt(phys_addr_t pa) { return (void *)(pa + PAGE_OFFSET); }

static inline phys_addr_t virt_to_phys(const void *va) {
  u64 v = (u64)va;
  if (v >= KIMAGE_VADDR) return v - kimage_voffset;
  return v - PAGE_OFFSET;
}

#define __va(pa) phys_to_virt((phys_addr_t)(pa))
#define __pa(va) virt_to_phys((const void *)(va))

static inline bool is_linear_addr(u64 va) {
  return va >= PAGE_OFFSET && va < PAGE_OFFSET + LINEAR_MAP_SIZE;
}
#endif

#endif
