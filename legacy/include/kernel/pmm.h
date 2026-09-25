#ifndef __KERNEL_PMM_H__
#define __KERNEL_PMM_H__

#include <types.h>

void pmm_init(uint64_t mem_start, uint64_t mem_size);
void* pmm_alloc_page(void);
void pmm_free_page(void* ptr);
uint64_t pmm_get_free_pages(void);

#endif // __KERNEL_PMM_H__
