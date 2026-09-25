#ifndef _SYS_MMAN_H_
#define _SYS_MMAN_H_

#include <stddef.h>

#define PROT_NONE      0
#define PROT_READ      1
#define PROT_WRITE     2
#define PROT_EXEC      4

#define MAP_SHARED     1
#define MAP_PRIVATE    2
#define MAP_ANONYMOUS  32
#define MAP_FAILED     ((void*)-1)

void* mmap(void* addr, size_t length, int prot, int flags, int fd, long offset);
int munmap(void* addr, size_t length);
int mprotect(void* addr, size_t length, int prot);

#endif
