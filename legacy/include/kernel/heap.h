#ifndef __KERNEL_HEAP_H__
#define __KERNEL_HEAP_H__

#include <types.h>

void heap_init(void);
void* kmalloc(uint64_t size);
void kfree(void* ptr);

#endif // __KERNEL_HEAP_H__
