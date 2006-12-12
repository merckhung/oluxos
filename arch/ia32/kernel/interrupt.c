/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
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
#include <ia32/io.h>
#include <ia32/console.h>
#include <ia32/i8259.h>
#include <string.h>


static struct ia32_IDTEntry_t IDTTable[ INTTBL_SIZE ];
extern void __code_seg( void );

extern void ia32_ExceptionHandler( void );


//
// ia32_IntInitInterrupt
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Initialize CPU IDT tables
//
void ia32_IntInitInterrupt( void ) {


    int i;
    struct ia32_IDTPtr_t IDTPtr;


    // Initialize
    memset( IDTTable, 0, INTTBL_SIZE );


    // Setup exceptions handler
    for( i = TRAP_START ; i <= TRAP_END ; i++ ) {
    
        ia32_IntSetIDT( i, (__u32)ia32_ExceptionHandler, (__u16)__code_seg, TRAP_GATE_FLAG );
    }


    // Setup IDT Pointer
    IDTPtr.Limit    = INTTBL_SIZE * INTENTRY_SIZE - 1;
    IDTPtr.BaseAddr = (__u32)IDTTable;


    // Load IDTR 
    __asm__ __volatile__ (  \
        "lidt   (%0)\n"     \
        :: "g" (&IDTPtr)    \
    );


    // Initialize i8259A PIC
    ia32_i8259Init();


    // Enable interrupt
    ia32_IntEnable();
}


//
// ia32_IntSetIDT
//
// Input:
//  index   : Interrupt number
//  offset  : Offset of interrupt handler from code segment
//  seg     : Segment descriptor offset
//  flag    : Flag of gate
//
// Return:
//  None
//
// Description:
//  Setup IDT entry for specified interrupt/exception number
//
void ia32_IntSetIDT( __u8 index, __u32 offset, __u16 seg, __u8 flag ) {

    IDTTable[ index ].OffsetLSW = (__u16)(offset & 0xffff);
    IDTTable[ index ].OffsetMSW = (__u16)((offset >> 16) & 0xffff);
    IDTTable[ index ].SegSelect = seg;
    IDTTable[ index ].Flag      = flag;
}


//
// ia32_IntDelIDT
//
// Input:
//  index   : Interrupt number
//
// Return:
//  None
//
// Description:
//  Delete IDT entry for specified interrupt/exception number
//
void ia32_IntDelIDT( __u8 index ) {

    memset( IDTTable + (index * 8), 0, INTENTRY_SIZE );
}


//
// ia32_IntDisable
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Disable CPU interrupt
//
void ia32_IntDisable( void ) {

    __asm__ ( "cli\n" );
}


//
// ia32_IntEnable
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Enable CPU interrupt
//
void ia32_IntEnable( void ) {

    __asm__ ( "sti" );
}


//
// ia32_IntRegIRQ
//
// Intput:
//  irqnum      : IRQ number
//  handler     : offset address of interrupt handler
//
// Return:
//  0  -- success
//  -1 -- failed
//
// Description:
//  Public routine for HW IRQ register
//
void ia32_IntRegIRQ( __u8 irqnum, __u32 handler ) {

#ifdef INT_DEBUG
    ia32_TcPrint( "ia32_IntRegIRQ: index 0x%x, handler 0x%x\n", irqnum + PIC_IRQ_BASE, handler );
#endif
    // Add interrupt gate
    ia32_IntSetIDT( irqnum + PIC_IRQ_BASE, handler, (__u16)__code_seg, INT_GATE_FLAG );
    // Enable 8259A IRQ line
    ia32_i8259EnableIRQ( irqnum );
}


//
// ia32_IntUnregisterIRQ
//
// Input:
//  irqnum      : IRQ number
//
// Return:
//  None
//
// Description:
//  Public routine for HW IRQ unregister
//
void ia32_IntUnregIRQ( __u8 irqnum ) {

#ifdef INT_DEBUG
    ia32_TcPrint( "ia32_IntUnregIRQ: index 0x%x\n", irqnum + PIC_IRQ_BASE );
#endif
    // Disable 8259A IRQ line
    ia32_i8259DisableIRQ( irqnum );
    // Delete interrupt gate
    ia32_IntDelIDT( irqnum + PIC_IRQ_BASE );
}


