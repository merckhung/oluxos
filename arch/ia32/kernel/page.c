/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * page.c -- OluxOS IA32 paging routines
 *
 */
#include <types.h>
#include <ia32/page.h>
#include <ia32/debug.h>


static volatile u32 *PDEPtr;
static volatile u32 *PTEPtr;
volatile u32  *usermem;


//
// MmPageInit
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
void MmPageInit( void ) {

    s32 i;
    u32 filladdr = 0;


    // Initialize Page Directory Entries
    PDEPtr = (u32 *)PAGEADDR;
    PTEPtr = (u32 *)((u32)PDEPtr + 0x1000);
    for( i = 0 ; i < PDENUM ; i++ ) {
    
        *(PDEPtr + i) = ((u32)PTEPtr + i * 0x1000) | 0x00000003;

        if( (i + 1) == PDENUM ) {
        
            //
            // Masked for User space
            //
            *(PDEPtr + i) |= 0x00000004;
        }

        //DbgPrint( "PDE value = 0x%8X\n", *(PDEPtr + i) );
    }


    // Initialize Page Table Entries
    for( i = 0 ; i < PTENUM ; i++ ) {

        *(PTEPtr + i) = (u32)(filladdr + i * 0x1000) | 0x00000003;

        if( i >= (PTENUM - 1024) ) {


            //
            // Masked for User space
            //
            *(PTEPtr + i) |= 0x00000004;
            //DbgPrint( "PTE value = 0x%8X\n", *(PTEPtr + i) );
            
            if( !usermem ) {
            
                //DbgPrint( "usermem 1 = 0x%8X, value 1 = 0x%8X\n", PTEPtr + i, *(PTEPtr + i) );
                usermem = PTEPtr + i;
            }
         }

        //DbgPrint( "PTE value = 0x%8X\n", *(PTEPtr + i) );
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



