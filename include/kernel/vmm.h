#ifndef __KERNEL_VMM_H__
#define __KERNEL_VMM_H__

#include <types.h>

#define VMM_FLAG_READ  (1 << 0)
#define VMM_FLAG_WRITE (1 << 1)
#define VMM_FLAG_EXEC  (1 << 2)
#define VMM_FLAG_USER  (1 << 3)
#define VMM_FLAG_DEVICE (1 << 4)

void vmm_init(void);
uint64_t vmm_create_aspace(void);
int vmm_map(uint64_t aspace, uint64_t va, uint64_t pa, uint64_t flags);
int vmm_unmap(uint64_t aspace, uint64_t va);
void vmm_free_aspace(uint64_t aspace);
void vmm_dump_path(uint64_t aspace, uint64_t va);

#endif // __KERNEL_VMM_H__
