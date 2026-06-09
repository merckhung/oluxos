#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <clib.h>

extern void ns16550_puts(const char* s);
extern void print_hex(uint32_t val);

extern uint32_t boot_pg_dir[];

#define PTE_V (1UL << 0)
#define PTE_R (1UL << 1)
#define PTE_W (1UL << 2)
#define PTE_X (1UL << 3)
#define PTE_U (1UL << 4)
#define PTE_A (1UL << 6)
#define PTE_D (1UL << 7)

static uint32_t translate_flags(uint64_t generic_flags) {
    uint32_t flags = PTE_V | PTE_A | PTE_D;
    if (generic_flags & VMM_FLAG_READ)  flags |= PTE_R;
    if (generic_flags & VMM_FLAG_WRITE) flags |= PTE_W;
    if (generic_flags & VMM_FLAG_EXEC)  flags |= PTE_X;
    if (generic_flags & VMM_FLAG_USER)  flags |= PTE_U;
    return flags;
}

void vmm_init(void) {
    ns16550_puts("VMM: Sv32 Virtual Memory Manager Initialized.\n");
}

uint64_t vmm_create_aspace(void) {
    void* root = pmm_alloc_page();
    if (!root) {
        ns16550_puts("VMM: Failed to allocate root page table!\n");
        return 0;
    }
    
    // Copy boot page table to inherit kernel mappings
    CbMemCpy((int8_t*)root, (int8_t*)boot_pg_dir, 4096);

    // Zero out userspace entries (0-16MB, L1 indices 0-3)
    uint32_t* l1 = (uint32_t*)root;
    l1[0] = 0;
    l1[1] = 0;
    l1[2] = 0;
    l1[3] = 0;

    uint32_t satp = (1UL << 31) | ((uint32_t)root >> 12);

    return (uint64_t)satp;
}

void vmm_free_aspace(uint64_t satp) {
    uint32_t satp32 = (uint32_t)satp;
    uint32_t root_phys = (satp32 & ((1UL << 22) - 1)) << 12;
    uint32_t* l1 = (uint32_t*)root_phys;

    // Free userspace page tables (indices 0-3)
    int i;
    for (i = 0; i < 4; i++) {
        if (l1[i] & PTE_V) {
            if ((l1[i] & (PTE_R | PTE_W | PTE_X)) == 0) {
                uint32_t* l0 = (uint32_t*)(((l1[i] >> 10) & ((1U << 22) - 1)) << 12);
                int j;
                for (j = 0; j < 1024; j++) {
                    if (l0[j] & PTE_V) {
                        if (l0[j] & PTE_U) {
                            uint32_t pa = ((l0[j] >> 10) & ((1U << 22) - 1)) << 12;
                            pmm_free_page((void*)pa);
                        }
                    }
                }
                pmm_free_page(l0);
            }
        }
    }
    pmm_free_page(l1);
}

int vmm_map(uint64_t satp, uint64_t va, uint64_t pa, uint64_t flags) {
    uint32_t satp32 = (uint32_t)satp;
    uint32_t va32 = (uint32_t)va;
    uint32_t pa32 = (uint32_t)pa;
    
    uint32_t root_phys = (satp32 & ((1UL << 22) - 1)) << 12;
    uint32_t* l1 = (uint32_t*)root_phys;

    uint32_t l1_idx = (va32 >> 22) & 0x3FF;
    uint32_t l0_idx = (va32 >> 12) & 0x3FF;

    uint32_t translated_flags = translate_flags(flags);

    // Level 1
    if ((l1[l1_idx] & PTE_V) == 0) {
        void* l0_table = pmm_alloc_page();
        if (!l0_table) return -1;
        l1[l1_idx] = (((uint32_t)l0_table & ~0xFFF) >> 12 << 10) | PTE_V;
    }
    uint32_t* l0 = (uint32_t*)(((l1[l1_idx] >> 10) & ((1U << 22) - 1)) << 12);

    // Level 0 (leaf)
    l0[l0_idx] = (((pa32 & ~0xFFF) >> 12) << 10) | translated_flags;

    return 0;
}

int vmm_unmap(uint64_t satp, uint64_t va) {
    uint32_t satp32 = (uint32_t)satp;
    uint32_t va32 = (uint32_t)va;
    
    uint32_t root_phys = (satp32 & ((1UL << 22) - 1)) << 12;
    uint32_t* l1 = (uint32_t*)root_phys;

    uint32_t l1_idx = (va32 >> 22) & 0x3FF;
    uint32_t l0_idx = (va32 >> 12) & 0x3FF;

    if ((l1[l1_idx] & PTE_V) == 0) return -1;
    uint32_t* l0 = (uint32_t*)(((l1[l1_idx] >> 10) & ((1U << 22) - 1)) << 12);

    if ((l0[l0_idx] & PTE_V) == 0) return -1;
    l0[l0_idx] = 0; // Clear entry

    return 0;
}
