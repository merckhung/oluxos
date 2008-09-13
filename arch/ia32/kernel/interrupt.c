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
#include <ia32/platform.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/i8259.h>
#include <ia32/debug.h>


IntHandlerLst InterrupHandlertList[ NR_VECTOR ];
IDTEntry IDTTable[ NR_VECTOR ];
IDTPtr IDTPointer;


extern void __KERNEL_CS( void );
extern void InterruptHandler( void );
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
extern void PageFaultHandler( void );
extern void x87_fpu_floating_point_error( void );
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

    u32 i;


	DbgPrint( "IDT Entry Size: 0x%8.8X\n", sizeof( IDTEntry ) );


    // Initialize IDT Tables
    CbMemSet( (s8 *)IDTTable, 0, sizeof( IDTEntry ) * NR_VECTOR );


    // Setup exceptions handler
    for( i = 0 ; i < HW_INT_START ; i++ ) {

        IntSetIDT( i, ExceptionHandler, __KERNEL_CS, GATE_TRAP_FLAG );
    }


	// Initialize IDT Entry for Exceptions
    IntSetIDT( 0, divide_error, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 1, debug, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 2, nmi, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 3, breakpoint, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 4, overflow, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 5, bound_range_exceeded, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 6, invalid_opcode, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 7, device_not_available, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 8, double_fault, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 9, coprocessor_segment_overrun, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 10, invalid_tss, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 11, segment_not_present, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 12, stack_fault, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 13, general_protection_exception, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 14, PageFaultHandler, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 16, x87_fpu_floating_point_error, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 17, alignment_check_exception, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 18, machine_check_exception, __KERNEL_CS, GATE_TRAP_FLAG );
    IntSetIDT( 19, simd_floating_point_exception, __KERNEL_CS, GATE_TRAP_FLAG );



    // Setup IDT Pointer
    IDTPointer.Limit    = (NR_VECTOR - 1) * sizeof( IDTEntry );
    IDTPointer.BaseAddr = (u32)IDTTable;
	DbgPrint( "IDTPointer = 0x%X, IDTTable = 0x%X\n", &IDTPointer, IDTTable );


	//Load IDT Pointer
	IntLoadIDTRegister( &IDTPointer );


    // Initialize i8259A PIC
    i8259Init();


    // Enable interrupt
	DbgPrint( "Enable Interrupt\n" );
    IntEnable();
}



void IntLoadIDTRegister( IDTPtr *Ptr ) {

    // Load IDT Register
    __asm__ __volatile__ (

        "lidt   (%%eax)\n"
		:: "a" (Ptr)
    );
}



//
// IntSetIDT
//
// Input:
//  index   : Interrupt number
//  offset  : Offset of interrupt handler from code segment
//  seg     : Segment descriptor offset
//  flag    : Flags of Gate
//
// Return:
//  None
//
// Description:
//  Setup IDT entry for specified interrupt/exception number
//
void IntSetIDT( u32 Index, void *Handler, void *SegSel, u8 Flags ) {

	u32 Offset = (u32)Handler;

    IDTTable[ Index ].OffsetLSW = (u16)(Offset & 0xFFFF);
    IDTTable[ Index ].OffsetMSW = (u16)((Offset >> 16) & 0xFFFF);
    IDTTable[ Index ].SegSelect = (u32)SegSel;
    IDTTable[ Index ].Flags		= Flags;
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
void IntDelIDT( u32 Index ) {

    CbMemSet( (s8 *)(&IDTTable[ Index ]), 0, sizeof( IDTEntry ) );
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
void IntRegInterrupt( u32 IrqNum, void *IrqHandler, void (*HwIntHandler)( u8 IrqNum ) ) {


    DbgPrint( "IntRegInterrupt: IRQ 0x%x, Comm 0x%x, Hw 0x%x\n", IrqNum, (u32)IrqHandler, (u32)HwIntHandler );


	// Disable interrupt
	IntDisable();


    // Add interrupt gate
    IntSetIDT( IrqNum + HW_INT_START, (u32)IrqHandler, __KERNEL_CS, GATE_INT_FLAG );


	IntShowIDTTable();


    // Install interrupt handler
    InterrupHandlertList[ IrqNum ].IrqHandler = HwIntHandler;


    // Enable 8259A IRQ line
    i8259EnableIRQ( IrqNum );


	// Enable interrupt
	IntEnable();
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
void IntUnregInterrupt( u32 IrqNum ) {


    DbgPrint( "IntUnregInterrupt: Index 0x%x\n", IrqNum );


	// Disable interrupt
	IntDisable();


    // Disable 8259A IRQ line
    i8259DisableIRQ( IrqNum );


    // Uninstall interrupt handler
    InterrupHandlertList[ IrqNum ].IrqHandler = 0;


    // Delete interrupt gate
    IntDelIDT( IrqNum + HW_INT_START );


	// Enable interrupt
	IntEnable();
}



/*
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
    IntSetIDT( irqnum + PIC_IRQ_BASE, handler, (u16)__KERNEL_CS, GATE_INT_FLAG );


    // Install interrupt handler
    InterrupHandlertList[ irqnum + PIC_IRQ_BASE ].IrqHandler = IRQHandler;


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
    InterrupHandlertList[ irqnum + PIC_IRQ_BASE ].IrqHandler = 0;


    // Delete interrupt gate
    IntDelIDT( irqnum + PIC_IRQ_BASE );
}
*/



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
void IntHandleIRQ( u32 IrqNum, GeneralRegisters *Regs ) {


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


    InterrupHandlertList[ IrqNum ].IrqHandler( IrqNum );
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



void IntShowIDTTable( void ) {

	u32 i;
	s32 *p;

	for( i = 0x20 ; i < 0x22 ; i++ ) {
	
		p = (s32 *)&IDTTable[ i ];
		DbgPrint( "Interrupt Number: %d\n", i );
		DbgPrint( "0x%8.8X%8.8X\n", *(p + 1), *p );
	}
}



