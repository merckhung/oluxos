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
extern void page_fault_exception( void );
extern void x86_fpu_floating_point_error( void );
extern void alignment_check_exception( void );
extern void machine_check_exception( void );
extern void simd_floating_point_exception( void );


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


    s32 i;
    struct IDTPtr_t IDTPtr;


    // Initialize
    CbMemSet( IDTTable, 0, SZ_INTENTRY );


    IntSetIDT( 0, (u32)divide_error, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 1, (u32)debug, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 2, (u32)nmi, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 3, (u32)breakpoint, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 4, (u32)overflow, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 5, (u32)bound_range_exceeded, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 6, (u32)invalid_opcode, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 7, (u32)device_not_available, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 8, (u32)double_fault, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 9, (u32)coprocessor_segment_overrun, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 10, (u32)invalid_tss, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 11, (u32)segment_not_present, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 12, (u32)stack_fault, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 13, (u32)general_protection_exception, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 14, (u32)page_fault_exception, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 15, (u32)ExceptionHandler, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 16, (u32)x86_fpu_floating_point_error, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 17, (u32)alignment_check_exception, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 18, (u32)machine_check_exception, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    IntSetIDT( 19, (u32)simd_floating_point_exception, (u16)__KERNEL_CS, TRAP_GATE_FLAG );



    // Setup exceptions handler
    for( i = 20 ; i <= TRAP_END ; i++ ) {

        IntSetIDT( i, (u32)ExceptionHandler, (u16)__KERNEL_CS, TRAP_GATE_FLAG );
    }


    // Setup IDT Pointer
    IDTPtr.Limit    = NR_INTERRUPT * SZ_INTENTRY - 1;
    IDTPtr.BaseAddr = (u32)IDTTable;


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
void IntSetIDT( u8 index, u32 offset, u16 seg, u8 flag ) {

    IDTTable[ index ].OffsetLSW = (u16)(offset & 0xffff);
    IDTTable[ index ].OffsetMSW = (u16)((offset >> 16) & 0xffff);
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
void IntDelIDT( u8 index ) {

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
void IntRegInterrupt( u8 intnum, u32 handler, void (*IntHandler)( u8 intnum ) ) {

    DbgPrint( "IntRegInterrupt: index 0x%x, handler 0x%x\n", intnum, handler );

    // Add interrupt gate
    IntSetIDT( intnum, handler, (u16)__KERNEL_CS, TRAP_GATE_FLAG );

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
void IntUnregInterrupt( u8 intnum ) {

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
void IntRegIRQ( u8 irqnum, u32 handler, void (*IRQHandler)( u8 irqnum ) ) {

    DbgPrint( "IntRegIRQ: index 0x%x, handler 0x%x\n", irqnum + PIC_IRQ_BASE, handler );

    // Add interrupt gate
    IntSetIDT( irqnum + PIC_IRQ_BASE, handler, (u16)__KERNEL_CS, INT_GATE_FLAG );

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
void IntUnregIRQ( u8 irqnum ) {

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
void IntHandleIRQ( u32 irqnum, struct SavedRegs_t regs ) {

    u8 irq = (u8)irqnum;

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


