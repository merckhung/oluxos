/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * interrupt.c -- OluxOS IA32 interrupt routines
 *
 */
#include <ia32/types.h>
#include <ia32/interrupt.h>
#include <string.h>


static struct ia32_IDTEntry_t IDTTable[ INTTBL_SIZE ];

extern void ia32_IntHandler( void );
extern void __code_seg( void );


void ia32_IntSetupIDT( void ) {


    int i;
    struct ia32_IDTPtr_t IDTPtr;


    // Initialize
    memset( IDTTable, 0, INTTBL_SIZE );


    // Setup exceptions handler
    for( i = 0 ; i < 20 ; i++ ) {
    
        ia32_IntSetIDT( i, (__u32)ia32_IntHandler, (__u16)__code_seg, 0x8f );
    }


    // Setup interrupt handler
    for( i = 32 ; i < 48 ; i++ ) {
    
        ia32_IntSetIDT( i, (__u32)ia32_IntHandler, (__u16)__code_seg, 0x8e );
    }


    // Setup IDT Pointer
    IDTPtr.Limit    = INTTBL_SIZE * INTENTRY_SIZE - 1;
    IDTPtr.BaseAddr = (__u32)IDTTable;


    // Load IDTR 
    __asm__ __volatile__ (  \
        "lidt   (%0)\n"     \
        :: "g" (&IDTPtr)    \
    );


    // Enable interrupt
    ia32_IntEnable();
}


void ia32_IntSetIDT( __u8 index, __u32 offset, __u16 seg, __u8 flag ) {

    IDTTable[ index ].OffsetLSW = (__u16)(offset & 0xffff);
    IDTTable[ index ].OffsetMSW = (__u16)((offset >> 16) & 0xffff);
    IDTTable[ index ].SegSelect = seg;
    IDTTable[ index ].Flag      = flag;
}


void ia32_IntDisable( void ) {

    __asm__ ( "cli\n" );
}


void ia32_IntEnable( void ) {

    __asm__ ( "sti" );
}


