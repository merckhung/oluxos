#include <kernel/heap.h>
#include <kernel/pmm.h>
#include <kernel/interrupt.h>
#include <clib.h>
#include <kernel/console.h>

extern void print_hex(uint64_t val);

typedef struct Header {
    struct Header* next;
    uint64_t size; // Size in units of sizeof(Header)
} Header;

static Header base;
static Header* freep = NULL;

#define NALLOC 1 // Allocate 1 page at a time

static Header* morecore(uint64_t nu) {
    uint64_t bytes = nu * sizeof(Header);
    uint64_t pages = (bytes + 4095) / 4096;
    if (pages < NALLOC) pages = NALLOC;
    
    if (pages > 1) {
        kputs("Heap: Request too large for non-contiguous PMM!\n");
        return NULL;
    }

    void* p = pmm_alloc_page();
    if (p == NULL) return NULL;

    Header* hp = (Header*)p;
    hp->size = pages * 4096 / sizeof(Header);
    
    kfree((void*)(hp + 1));
    return freep;
}

void heap_init(void) {
    base.next = freep = &base;
    base.size = 0;
    kputs("Heap initialized.\n");
}

void* kmalloc(uint64_t nbytes) {
    Header *p, *prevp;
    uint64_t nunits;

    if (nbytes == 0) return NULL;

    if (nbytes > 4096 - sizeof(Header)) {
        kputs("Heap: kmalloc request too large: ");
        print_hex(nbytes);
        kputs("\n");
        return NULL;
    }

    nunits = (nbytes + sizeof(Header) - 1) / sizeof(Header) + 1;

    IntDisable();

    if ((prevp = freep) == NULL) {
        base.next = freep = &base;
        base.size = 0;
        prevp = freep;
    }

    for (p = prevp->next;; prevp = p, p = p->next) {
        if (p->size >= nunits) {
            if (p->size == nunits) {
                prevp->next = p->next;
            } else {
                p->size -= nunits;
                p += p->size;
                p->size = nunits;
            }
            freep = prevp;
            IntEnable();
            return (void*)(p + 1);
        }
        if (p == freep) {
            if ((p = morecore(nunits)) == NULL) {
                IntEnable();
                return NULL;
            }
        }
    }
}

void kfree(void* ap) {
    Header *bp, *p;

    if (ap == NULL) return;

    bp = (Header*)ap - 1;

    IntDisable();

    for (p = freep; !(bp > p && bp < p->next); p = p->next) {
        if (p >= p->next && (bp > p || bp < p->next)) {
            break;
        }
    }

    if (bp + bp->size == p->next) {
        bp->size += p->next->size;
        bp->next = p->next->next;
    } else {
        bp->next = p->next;
    }

    if (p + p->size == bp) {
        p->size += bp->size;
        p->next = bp->next;
    } else {
        p->next = bp;
    }

    freep = p;
    IntEnable();
}
