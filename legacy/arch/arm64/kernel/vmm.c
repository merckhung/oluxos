#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <clib.h>

extern void pl011_puts(const char* s);
extern void print_hex(uint64_t val);

extern uint64_t pg_dir[];

// ARM64 Translation Table Descriptor bits
#define PTE_VALID       (1ULL << 0)
#define PTE_TABLE       (1ULL << 1)
#define PTE_PAGE        (1ULL << 1)
#define PTE_USER        (1ULL << 6)   // AP[1]
#define PTE_RO          (1ULL << 7)   // AP[2]
#define PTE_SH_INNER    (3ULL << 8)
#define PTE_AF          (1ULL << 10)
#define PTE_ATTR_NORMAL (1ULL << 2)   // Attr1 in MAIR
#define PTE_PXN         (1ULL << 53)
#define PTE_UXN         (1ULL << 54)
#define PTE_ADDR_MASK   0x0000FFFFFFFFF000ULL

static uint64_t translate_flags(uint64_t generic_flags) {
    uint64_t flags = PTE_VALID | PTE_PAGE | PTE_AF | PTE_SH_INNER;
    
    if (generic_flags & VMM_FLAG_DEVICE) {
        // Attr0 is Device memory (PTE_ATTR_NORMAL which is Attr1 is NOT set)
    } else {
        flags |= PTE_ATTR_NORMAL;
    }

    if (generic_flags & VMM_FLAG_USER) {
        flags |= PTE_USER;
        flags |= PTE_PXN;
    }
    
    if (!(generic_flags & VMM_FLAG_WRITE)) {
        flags |= PTE_RO;
    }
    
    if (!(generic_flags & VMM_FLAG_EXEC)) {
        flags |= PTE_UXN;
    }
    
    return flags;
}

void vmm_init(void) {
    pl011_puts("VMM: ARM64 Virtual Memory Manager Initialized.\n");
}

uint64_t vmm_create_aspace(void) {
    uint64_t* l1 = pmm_alloc_page();
    if (!l1) return 0;
    
    uint64_t* l2 = pmm_alloc_page();
    if (!l2) {
        pmm_free_page(l1);
        return 0;
    }
    
    // Link L1[0] -> L2 (Table descriptor)
    l1[0] = ((uint64_t)l2 & ~0xFFF) | PTE_VALID | PTE_TABLE;
    
    // Copy kernel RAM mapping (1-2GB)
    l1[1] = pg_dir[1];
    
#if CONFIG_BOARD_RPI4
    extern uint64_t l2_table_3[];
    l1[3] = ((uint64_t)l2_table_3 & ~0xFFF) | PTE_VALID | PTE_TABLE;
#else
    // QEMU Virt Peripherals
    l2[64] = 0x0060000008000401ULL; // GIC (Device, 2MB block)
    l2[72] = 0x0060000009000401ULL; // UART (Device, 2MB block)
#endif

    return (uint64_t)l1;
}

void vmm_free_aspace(uint64_t aspace) {
#if 0
    pl011_puts("vmm_free_aspace: "); print_hex(aspace); pl011_puts("\n");
#endif
    uint64_t* l1 = (uint64_t*)aspace;
    
    if (l1[0] & PTE_VALID) {
        uint64_t* l2 = (uint64_t*)(l1[0] & PTE_ADDR_MASK);
#if 0
        pl011_puts("  l2="); print_hex((uint64_t)l2); pl011_puts("\n");
#endif
        int i;
        for (i = 0; i < 512; i++) {
            // Check if it is a table descriptor (not block)
            if ((l2[i] & (PTE_VALID | PTE_TABLE)) == (PTE_VALID | PTE_TABLE)) {
                uint64_t* l3 = (uint64_t*)(l2[i] & PTE_ADDR_MASK);
#if 0
                pl011_puts("    l3["); print_hex(i); pl011_puts("]="); print_hex((uint64_t)l3); pl011_puts("\n");
#endif
                int j;
                for (j = 0; j < 512; j++) {
                    if (l3[j] & PTE_VALID) {
                        if (l3[j] & PTE_USER) {
                            uint64_t pa = l3[j] & PTE_ADDR_MASK;
                            pmm_free_page((void*)pa);
                        }
                    }
                }
#if 0
                pl011_puts("    free l3 "); print_hex((uint64_t)l3); pl011_puts("\n");
#endif
                pmm_free_page(l3);
            }
        }
#if 0
        pl011_puts("  free l2 "); print_hex((uint64_t)l2); pl011_puts("\n");
#endif
        pmm_free_page(l2);
    }
#if 0
    pl011_puts("  free l1 "); print_hex((uint64_t)l1); pl011_puts("\n");
#endif
    pmm_free_page(l1);
#if 0
    pl011_puts("vmm_free_aspace done\n");
#endif
}

int vmm_map(uint64_t aspace, uint64_t va, uint64_t pa, uint64_t flags) {
    uint64_t* l1 = (uint64_t*)aspace;
    
    uint32_t l1_idx = (va >> 30) & 0x1FF;
    uint32_t l2_idx = (va >> 21) & 0x1FF;
    uint32_t l3_idx = (va >> 12) & 0x1FF;
    
    // Level 1
    if ((l1[l1_idx] & PTE_VALID) == 0) {
        void* l2_table = pmm_alloc_page();
        if (!l2_table) return -1;
        l1[l1_idx] = ((uint64_t)l2_table & ~0xFFF) | PTE_VALID | PTE_TABLE;
    }
    uint64_t* l2 = (uint64_t*)(l1[l1_idx] & ~0xFFF);
    
    // Level 2
    if ((l2[l2_idx] & PTE_VALID) == 0) {
        void* l3_table = pmm_alloc_page();
        if (!l3_table) return -1;
        l2[l2_idx] = ((uint64_t)l3_table & ~0xFFF) | PTE_VALID | PTE_TABLE;
    }
    uint64_t* l3 = (uint64_t*)(l2[l2_idx] & ~0xFFF);
    
    // Level 3 (leaf page)
    uint64_t translated_flags = translate_flags(flags);
    l3[l3_idx] = (pa & ~0xFFF) | translated_flags;
    
    return 0;
}

int vmm_unmap(uint64_t aspace, uint64_t va) {
    uint64_t* l1 = (uint64_t*)aspace;
    
    uint32_t l1_idx = (va >> 30) & 0x1FF;
    uint32_t l2_idx = (va >> 21) & 0x1FF;
    uint32_t l3_idx = (va >> 12) & 0x1FF;
    
    if ((l1[l1_idx] & PTE_VALID) == 0) return -1;
    uint64_t* l2 = (uint64_t*)(l1[l1_idx] & ~0xFFF);
    
    if ((l2[l2_idx] & PTE_VALID) == 0) return -1;
    uint64_t* l3 = (uint64_t*)(l2[l2_idx] & ~0xFFF);
    
    if ((l3[l3_idx] & PTE_VALID) == 0) return -1;
    l3[l3_idx] = 0;
    
    return 0;
}

void vmm_dump_path(uint64_t aspace, uint64_t va) {
    uint64_t* l1 = (uint64_t*)aspace;
    uint32_t l1_idx = (va >> 30) & 0x1FF;
    uint32_t l2_idx = (va >> 21) & 0x1FF;
    uint32_t l3_idx = (va >> 12) & 0x1FF;
    
    pl011_puts("VMM Dump for "); print_hex(va); pl011_puts(":\n");
    pl011_puts("  L1: "); print_hex((uint64_t)l1); pl011_puts(" ["); print_hex(l1_idx); pl011_puts("] = "); print_hex(l1[l1_idx]); pl011_puts("\n");
    uint64_t l1_val = l1[l1_idx];
    uint64_t l1_type = l1_val & 3;
    pl011_puts("  L1 type="); print_hex(l1_type); pl011_puts("\n");
    
    if (l1_type == 3) {
        uint64_t* l2 = (uint64_t*)(l1[l1_idx] & ~0xFFF);
        pl011_puts("  L2: "); print_hex((uint64_t)l2); pl011_puts(" ["); print_hex(l2_idx); pl011_puts("] = "); print_hex(l2[l2_idx]); pl011_puts("\n");
        
        if ((l2[l2_idx] & 3) == 3) {
            uint64_t* l3 = (uint64_t*)(l2[l2_idx] & ~0xFFF);
            pl011_puts("  L3: "); print_hex((uint64_t)l3); pl011_puts(" ["); print_hex(l3_idx); pl011_puts("] = "); print_hex(l3[l3_idx]); pl011_puts("\n");
        } else if ((l2[l2_idx] & 3) == 1) {
            pl011_puts("  L2 is 2MB Block mapping to physical: ");
            print_hex(l2[l2_idx] & ~0x1FFFFF);
            pl011_puts("\n");
        }
    } else if (l1_type == 1) {
        pl011_puts("  L1 is 1GB Block mapping to physical: ");
        print_hex(l1[l1_idx] & ~0x3FFFFFFF);
        pl011_puts("\n");
    }
}

uint64_t vmm_dup_aspace(uint64_t src_aspace) {
    uint64_t dst_aspace = vmm_create_aspace();
    if (dst_aspace == 0) return 0;

    uint64_t* src_l1 = (uint64_t*)src_aspace;
    uint64_t* dst_l1 = (uint64_t*)dst_aspace;

    if (src_l1[0] & PTE_VALID) {
        uint64_t* src_l2 = (uint64_t*)(src_l1[0] & PTE_ADDR_MASK);
        uint64_t* dst_l2 = (uint64_t*)(dst_l1[0] & PTE_ADDR_MASK);

        int i;
        for (i = 0; i < 512; i++) {
            if ((src_l2[i] & (PTE_VALID | PTE_TABLE)) == (PTE_VALID | PTE_TABLE)) {
                uint64_t* src_l3 = (uint64_t*)(src_l2[i] & PTE_ADDR_MASK);
                
                uint64_t* dst_l3 = pmm_alloc_page();
                if (!dst_l3) {
                    vmm_free_aspace(dst_aspace);
                    return 0;
                }
                dst_l2[i] = ((uint64_t)dst_l3 & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE;

                int j;
                for (j = 0; j < 512; j++) {
                    if (src_l3[j] & PTE_VALID) {
                        if (src_l3[j] & PTE_USER) {
                            uint64_t src_pa = src_l3[j] & PTE_ADDR_MASK;
                            uint64_t* dst_page = pmm_alloc_page();
                            if (!dst_page) {
                                vmm_free_aspace(dst_aspace);
                                return 0;
                            }
                            CbMemCpy((int8_t*)dst_page, (const int8_t*)src_pa, 4096);
                            dst_l3[j] = ((uint64_t)dst_page & PTE_ADDR_MASK) | (src_l3[j] & 0xFFF0000000000FFFULL);
                        } else {
                            dst_l3[j] = src_l3[j];
                        }
                    }
                }
            }
        }
    }
    return dst_aspace;
}

