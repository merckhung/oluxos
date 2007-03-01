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
#include <types.h>
#include <clib.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/i8259.h>
#include <ia32/debug.h>

static struct IntHandlerLst_t InterrupHandlertList[ NR_INTERRUPT ];
static struct IDTEntry_t IDTTable[ SZ_INTENTRY ];
extern void __code_seg( void );

extern void ExceptionHandler( void );


//
// IntInitInterrupt
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
void IntInitInterrupt( void ) {


    int i;
    struct IDTPtr_t IDTPtr;


    // Initialize
    memset( IDTTable, 0, SZ_INTENTRY );


    // Setup exceptions handler
    for( i = TRAP_START ; i <= TRAP_END ; i++ ) {
    
        IntSetIDT( i, (__u32)ExceptionHandler, (__u16)__code_seg, TRAP_GATE_FLAG );
    }


    // Setup IDT Pointer
    IDTPtr.Limit    = NR_INTERRUPT * SZ_INTENTRY - 1;
    IDTPtr.BaseAddr = (__u32)IDTTable;


    // Load IDTR 
    __asm__ __volatile__ (

        "lidt   (%0)\n"
        :: "g" (&IDTPtr)
    );


    // Initialize i8259A PIC
    i8259Init();


    // Enable interrupt
    IntEnable();
}


//
// IntSetIDT
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
void IntSetIDT( __u8 index, __u32 offset, __u16 seg, __u8 flag ) {

    IDTTable[ index ].OffsetLSW = (__u16)(offset & 0xffff);
    IDTTable[ index ].OffsetMSW = (__u16)((offset >> 16) & 0xffff);
    IDTTable[ index ].SegSelect = seg;
    IDTTable[ index ].Flag      = flag;
}


//
// IntDelIDT
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
void IntDelIDT( __u8 index ) {

    memset( IDTTable + (index * 8), 0, SZ_INTENTRY );
}


//
// IntDisable
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
void IntDisable( void ) {

    __asm__ ( "cli\n" );
}


//
// IntEnable
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
void IntEnable( void ) {

    __asm__ ( "sti" );
}


//
// IntRegInterrupt
//
// Intput:
//  intnum      : Interrupt vector number
//  handler     : offset address of interrupt handler
//
// Return:
//  0  -- success
//  -1 -- failed
//
// Description:
//  Public routine for CPU interrupt register
//
void IntRegInterrupt( __u8 intnum, __u32 handler, void (*IntHandler)( __u8 intnum ) ) {

    DbgPrint( "IntRegInterrupt: index 0x%x, handler 0x%x\n", intnum, handler );

    // Add interrupt gate
    IntSetIDT( intnum, handler, (__u16)__code_seg, TRAP_GATE_FLAG );

    // Install interrupt handler
    InterrupHandlertList[ intnum ].Handler = IntHandler;
}


//
// IntUnregInterrupt
//
// Input:
//  intnum      : Interrupt vector number
//
// Return:
//  None
//
// Description:
//  Public routine for CPU interrupt register
//
void IntUnregInterrupt( __u8 intnum ) {

    DbgPrint( "IntUnregInterrupt: index 0x%x\n", intnum );

    // Uninstall interrupt handler
    InterrupHandlertList[ intnum ].Handler = 0;

    // Delete interrupt gate
    IntDelIDT( intnum );
}


//
// IntRegIRQ
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
void IntRegIRQ( __u8 irqnum, __u32 handler, void (*IRQHandler)( __u8 irqnum ) ) {

    DbgPrint( "IntRegIRQ: index 0x%x, handler 0x%x\n", irqnum + PIC_IRQ_BASE, handler );

    // Add interrupt gate
    IntSetIDT( irqnum + PIC_IRQ_BASE, handler, (__u16)__code_seg, INT_GATE_FLAG );

    // Install interrupt handler
    InterrupHandlertList[ irqnum + PIC_IRQ_BASE ].Handler = IRQHandler;

    // Enable 8259A IRQ line
    i8259EnableIRQ( irqnum );
}


//
// IntUnregIRQ
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
void IntUnregIRQ( __u8 irqnum ) {

    DbgPrint( "IntUnregIRQ: index 0x%x\n", irqnum + PIC_IRQ_BASE );

    // Disable 8259A IRQ line
    i8259DisableIRQ( irqnum );

    // Uninstall interrupt handler
    InterrupHandlertList[ irqnum + PIC_IRQ_BASE ].Handler = 0;

    // Delete interrupt gate
    IntDelIDT( irqnum + PIC_IRQ_BASE );
}


//
// IntHandleIRQ
//
// Input:
//  irqnum      : IRQ number
//
// Output:
//  None
//
// Description:
//  IRQ handler
//
void IntHandleIRQ( __u32 irqnum ) {

    __u8 irq = (__u8)irqnum;
    InterrupHandlertList[ irq + PIC_IRQ_BASE ].Handler( irq );
}


//
// IntIssueEOI
//
// Input:
//  None
//
// Output:
//  None
//
// Description:
//  Issue End of Interrupt action
//
void IntIssueEOI( void ) {

    i8259IssueEOI();
}


