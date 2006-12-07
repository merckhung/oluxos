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


static struct ia32_IDTEntry_t IDTTable[ INTTBL_SIZE ];


void ia32_IntInitHandler( void ) {





}


void ia32_IntSetupIDT( void ) {


    int i;
    struct ia32_IDTPtr_t IDTPtr;


    // Setup exceptions handler
    for( i = 0 ; i < 20 ; i++ ) {
    
        IDTTable[ i ].Flag      = 0x8f;
        IDTTable[ i ].SegSelect = 0x08;
        IDTTable[ i ].OffsetLSW = (__u16)(((__u32)ia32_IntHandler) & 0xffff);
        IDTTable[ i ].OffsetMSW = (__u16)((((__u32)ia32_IntHandler) >> 16) && 0xffff);
   }


    // Setup interrupt handler
    for( i = 32 ; i < 48 ; i++ ) {
    
        IDTTable[ i ].Flag      = 0x8e;
        IDTTable[ i ].SegSelect = 0x08;
        IDTTable[ i ].OffsetLSW = (__u16)(((__u32)ia32_IntHandler) & 0xffff);
        IDTTable[ i ].OffsetMSW = (__u16)((((__u32)ia32_IntHandler) >> 16) && 0xffff);
    }


    // Setup IDTR
    IDTPtr.Limit    = INTTBL_SIZE * INTENTRY_SIZE - 1;
    IDTPtr.BaseAddr = (__u32)IDTTable;


    __asm__ __volatile__ (  \
        "lidt   (%0)\n"     \
        :: "g" (&IDTPtr)    \
    );


    // Enable interrupt
    //ia32_IoSti();
}


void ia32_IntSetIDT( __u8 index, __u32 offset, __u16 seg, __u8 flag ) {

    IDTTable[ index ].OffsetLSW = (__u16)(offset & 0xffff);
    IDTTable[ index ].OffsetMSW = (__u16)((offset >> 16) & 0xffff);
    IDTTable[ index ].SegSelect = seg;
    IDTTable[ index ].Flag      = flag;
}


