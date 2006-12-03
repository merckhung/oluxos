/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * page.c -- OluxOS IA32 paging routines
 *
 */
#include <ia32/page.h>


static volatile unsigned long int *PDEPtr;
static volatile unsigned long int *PTEPtr;


void ia32_MmPageInit( void ) {

    int i;
    unsigned long int filladdr = 0;


    // Initialize Page Directory Entries
    PDEPtr = (unsigned long int *)PAGEADDR;
    PTEPtr = (unsigned long int *)((unsigned long int)PDEPtr + 0x1000);
    for( i = 0 ; i < PDENUM ; i++ ) {
    
        *(PDEPtr + i) = ((unsigned long int)PTEPtr + i * 0x1000) | 0x00000003;
    }


    // Initialize Page Table Entries
    for( i = 0 ; i < PTENUM ; i++ ) {

        *(PTEPtr + i) = (unsigned long int)(filladdr + i * 0x1000) | 0x00000003;
    }


    // Setup CR3 and enable Page
    __asm__ __volatile__ (
        "movl   %0, %%cr3\n"
        "movl   %%cr0, %%eax\n"
        "orl    $0x80000000, %%eax\n"
        "movl   %%eax, %%cr0\n"
        "jmp    _FlushTLBs\n"
        "_FlushTLBs:\n"
        :
        : "g" (PDEPtr)
        : "eax"
    );


}



