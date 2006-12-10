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
#include <ia32/types.h>
#include <ia32/page.h>


static volatile __u32 *PDEPtr;
static volatile __u32 *PTEPtr;


//
// ia32_MmPageInit
//
// Input:
//  None
//
// Output:
//  None
//
// Description:
//  Initialize page directories and page tables
//
void ia32_MmPageInit( void ) {

    int i;
    __u32 filladdr = 0;


    // Initialize Page Directory Entries
    PDEPtr = (__u32 *)PAGEADDR;
    PTEPtr = (__u32 *)((__u32)PDEPtr + 0x1000);
    for( i = 0 ; i < PDENUM ; i++ ) {
    
        *(PDEPtr + i) = ((__u32)PTEPtr + i * 0x1000) | 0x00000003;
    }


    // Initialize Page Table Entries
    for( i = 0 ; i < PTENUM ; i++ ) {

        *(PTEPtr + i) = (__u32)(filladdr + i * 0x1000) | 0x00000003;
    }


    // Setup CR3 and enable Page
    __asm__ __volatile__ (
        "movl   %0, %%eax\n"
        "movl   %%eax, %%cr3\n"
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



