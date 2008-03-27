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
extern void __KERNEL_CS( void );

extern void ExceptionHandler( void );

extern void divide_error( void );
extern void debug( void );
extern void nmi( void );
extern void breakpoint( void );
extern void overflow( void );
extern void bound_range_exceeded( void );
extern void invalid_opcode( void );
extern void device_not_available( void );
extern void double_fault( void );
extern void coprocessor_segment_overrun( void );
extern void invalid_tss( void );
extern void segment_not_present( void );
extern void stack_fault( void );
extern void general_protection_exception( void );


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
    CbMemSet( IDTTable, 0, SZ_INTENTRY );


    IntSetIDT( 0, (__u32)divide_error, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 1, (__u32)debug, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 2, (__u32)nmi, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 3, (__u32)breakpoint, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 4, (__u32)overflow, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 5, (__u32)bound_range_exceeded, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 6, (__u32)invalid_opcode, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 7, (__u32)device_not_available, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 8, (__u32)double_fault, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 9, (__u32)coprocessor_segment_overrun, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 10, (__u32)invalid_tss, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 11, (__u32)segment_not_present, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 12, (__u32)stack_fault, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 13, (__u32)general_protection_exception, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );


    // Setup exceptions handler
    for( i = 14 ; i <= TRAP_END ; i++ ) {

        IntSetIDT( i, (__u32)ExceptionHandler, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );
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

    CbMemSet( IDTTable + (index * 8), 0, SZ_INTENTRY );
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
    IntSetIDT( intnum, handler, (__u16)__KERNEL_CS, TRAP_GATE_FLAG );

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
    IntSetIDT( irqnum + PIC_IRQ_BASE, handler, (__u16)__KERNEL_CS, INT_GATE_FLAG );

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
void IntHandleIRQ( __u32 irqnum, struct SavedRegs_t regs ) {

    __u8 irq = (__u8)irqnum;

#if 0
    __asm__ __volatile__ (
    
        "movl   %0, %%eax\n"
        "movl   %%esp, %%ebx\n"
        "movl   %1, %%esp\n"
        "pushl  %%eax\n"
        "movl   %2, %%eax\n"
        "call   *(%%eax)\n"
        "addl   $4, %%esp\n"
        "jmp    .\n"
        :: "g" (irqnum),
           "g" (IRQStack.esp),
           "g" (InterrupHandlertList[ irq + PIC_IRQ_BASE ].Handler)
        : "eax"
    );
#endif

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


