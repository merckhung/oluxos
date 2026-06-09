#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <clib.h>

extern void ns16550_puts(const char* s);
extern void print_hex(uint64_t val);

extern uint64_t boot_pg_dir[];

#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_U (1ULL << 4)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)

static uint64_t translate_flags(uint64_t generic_flags) {
    uint64_t flags = PTE_V | PTE_A | PTE_D;
    if (generic_flags & VMM_FLAG_READ)  flags |= PTE_R;
    if (generic_flags & VMM_FLAG_WRITE) flags |= PTE_W;
    if (generic_flags & VMM_FLAG_EXEC)  flags |= PTE_X;
    if (generic_flags & VMM_FLAG_USER)  flags |= PTE_U;
    return flags;
}

void vmm_init(void) {
    ns16550_puts("VMM: Sv39 Virtual Memory Manager Initialized.\n");
}

uint64_t vmm_create_aspace(void) {
    void* root = pmm_alloc_page();
    if (!root) {
        ns16550_puts("VMM: Failed to allocate root page table!\n");
        return 0;
    }
    CbMemSet((int8_t*)root, 0, 4096);

    uint64_t* l2 = (uint64_t*)root;

    // Copy RAM mapping from boot_pg_dir to allow kernel access in S-mode
    l2[2] = boot_pg_dir[2];

    uint64_t satp = (8ULL << 60) | ((uint64_t)root >> 12);

    // Map UART (4KB) for S-mode access
    vmm_map(satp, 0x10000000, 0x10000000, VMM_FLAG_READ | VMM_FLAG_WRITE);

    return satp;
}

int vmm_map(uint64_t satp, uint64_t va, uint64_t pa, uint64_t flags) {
    uint64_t root_phys = (satp & ((1ULL << 44) - 1)) << 12;
    uint64_t* l2 = (uint64_t*)root_phys;

    uint32_t l2_idx = (va >> 30) & 0x1FF;
    uint32_t l1_idx = (va >> 21) & 0x1FF;
    uint32_t l0_idx = (va >> 12) & 0x1FF;

    uint64_t translated_flags = translate_flags(flags);

    // Level 2
    if ((l2[l2_idx] & PTE_V) == 0) {
        void* l1_table = pmm_alloc_page();
        if (!l1_table) return -1;
        CbMemSet((int8_t*)l1_table, 0, 4096);
        l2[l2_idx] = (((uint64_t)l1_table & ~0xFFF) >> 12 << 10) | PTE_V;
    }
    uint64_t* l1 = (uint64_t*)(((l2[l2_idx] >> 10) & ((1ULL << 44) - 1)) << 12);

    // Level 1
    if ((l1[l1_idx] & PTE_V) == 0) {
        void* l0_table = pmm_alloc_page();
        if (!l0_table) return -1;
        CbMemSet((int8_t*)l0_table, 0, 4096);
        l1[l1_idx] = (((uint64_t)l0_table & ~0xFFF) >> 12 << 10) | PTE_V;
    }
    uint64_t* l0 = (uint64_t*)(((l1[l1_idx] >> 10) & ((1ULL << 44) - 1)) << 12);

    // Level 0 (leaf)
    l0[l0_idx] = (((pa & ~0xFFF) >> 12) << 10) | translated_flags;

    return 0;
}

int vmm_unmap(uint64_t satp, uint64_t va) {
    uint64_t root_phys = (satp & ((1ULL << 44) - 1)) << 12;
    uint64_t* l2 = (uint64_t*)root_phys;

    uint32_t l2_idx = (va >> 30) & 0x1FF;
    uint32_t l1_idx = (va >> 21) & 0x1FF;
    uint32_t l0_idx = (va >> 12) & 0x1FF;

    if ((l2[l2_idx] & PTE_V) == 0) return -1;
    uint64_t* l1 = (uint64_t*)(((l2[l2_idx] >> 10) & ((1ULL << 44) - 1)) << 12);

    if ((l1[l1_idx] & PTE_V) == 0) return -1;
    uint64_t* l0 = (uint64_t*)(((l1[l1_idx] >> 10) & ((1ULL << 44) - 1)) << 12);

    l0[l0_idx] = 0; // Clear entry
    
    // We could free empty L0/L1 tables here if we wanted to be memory efficient.
    // For now we keep it simple.
    return 0;
}

void vmm_free_aspace(uint64_t satp) {
    uint64_t root_phys = (satp & ((1ULL << 44) - 1)) << 12;
    uint64_t* l2 = (uint64_t*)root_phys;

    // We need to recursively free all page tables allocated for this address space.
    // We must NOT free the leaf pages mapped in L0 (userspace memory), only the page tables themselves.
    // We must NOT free boot_pg_dir copy or UART mapping if it was not allocated.
    // UART is a block leaf in L1, so it has no L0 table.
    // L2[2] is copy of boot_pg_dir[2], we do NOT follow it.
    
    // Scan L2
    int i, j;
    for (i = 0; i < 512; i++) {
        if (i == 2) continue; // Skip kernel copy
        if (l2[i] & PTE_V) {
            uint64_t* l1 = (uint64_t*)(((l2[i] >> 10) & ((1ULL << 44) - 1)) << 12);
            // Scan L1
            for (j = 0; j < 512; j++) {
                if (i == 0 && j == 128) continue; // Skip UART leaf
                if (l1[j] & PTE_V) {
                    // Check if it's a leaf block (like UART, but we skipped it. Just in case)
                    if ((l1[j] & (PTE_R | PTE_W | PTE_X)) == 0) {
                        uint64_t* l0 = (uint64_t*)(((l1[j] >> 10) & ((1ULL << 44) - 1)) << 12);
                        int k;
                        for (k = 0; k < 512; k++) {
                            if (l0[k] & PTE_V) {
                                if (l0[k] & PTE_U) {
                                    uint64_t pa = ((l0[k] >> 10) & ((1ULL << 44) - 1)) << 12;
                                    pmm_free_page((void*)pa);
                                }
                            }
                        }
                        pmm_free_page((void*)l0);
                    }
                }
            }
            pmm_free_page((void*)l1);
        }
    }
    pmm_free_page((void*)l2);
}
