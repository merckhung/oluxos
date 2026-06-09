#include <types.h>
#include <clib.h>
#include <kernel/spinlock.h>
#include <kernel/interrupt.h>
#include <kernel/console.h>

extern void print_hex(uint64_t val);

typedef struct Page {
    struct Page* next;
} Page;

static Page* free_list = NULL;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint64_t pmm_start = 0;
static uint64_t pmm_end = 0;
static Spinlock pmm_lock;

void pmm_init(uint64_t mem_start, uint64_t mem_size) {
    spin_init(&pmm_lock);
    uint64_t start = (mem_start + 4095) & ~4095; // Align to 4KB
    uint64_t end = (mem_start + mem_size) & ~4095;
    
    pmm_start = start;
    pmm_end = end;
    
    kputs("PMM Init: range ");
    print_hex(start);
    kputs(" - ");
    print_hex(end);
    kputs("\n");

    uint64_t addr;
    for (addr = start; addr + 4096 <= end; addr += 4096) {
        Page* page = (Page*)(uintptr_t)addr;
        page->next = free_list;
        free_list = page;
        total_pages++;
        free_pages++;
    }

    kputs("PMM Init: total pages ");
    print_hex(total_pages);
    kputs(" free pages ");
    print_hex(free_pages);
    kputs("\n");
}

void* pmm_alloc_page(void) {
    IntDisable();
    spin_lock(&pmm_lock);
    if (free_list == NULL) {
        kputs("PMM: Out of memory!\n");
        spin_unlock(&pmm_lock);
        IntEnable();
        return NULL;
    }
    Page* page = free_list;
    free_list = page->next;
    free_pages--;
    spin_unlock(&pmm_lock);
    IntEnable();
    
    // Clear page content outside the lock to reduce contention
    CbMemSet((int8_t*)page, 0, 4096);
    
    return (void*)page;
}

void pmm_free_page(void* ptr) {
    if (ptr == NULL) return;
    if ((uintptr_t)ptr & 4095) {
        kputs("PMM: Freeing unaligned page!\n");
        return;
    }
    if ((uintptr_t)ptr < pmm_start || (uintptr_t)ptr >= pmm_end) {
        // Not in managed range, silently ignore (or log if debug)
        return;
    }
    IntDisable();
    spin_lock(&pmm_lock);
    Page* page = (Page*)ptr;
    page->next = free_list;
    free_list = page;
    free_pages++;
    spin_unlock(&pmm_lock);
    IntEnable();
}

uint64_t pmm_get_free_pages(void) {
    return free_pages;
}
