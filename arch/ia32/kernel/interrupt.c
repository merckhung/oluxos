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
#include <ia32/io.h>
#include <ia32/console.h>
#include <string.h>

#define DEBUG   1

static struct ia32_IDTEntry_t IDTTable[ INTTBL_SIZE ];

extern void ia32_IntHandler( void );
extern void __code_seg( void );


//
// ia32_IntSetupIDT
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
void ia32_IntSetupIDT( void ) {


    int i;
    struct ia32_IDTPtr_t IDTPtr;


    // Initialize
    memset( IDTTable, 0, INTTBL_SIZE );


    // Setup exceptions handler
    for( i = TRAP_START ; i <= TRAP_END ; i++ ) {
    
        ia32_IntSetIDT( i, (__u32)ia32_IntHandler, (__u16)__code_seg, 0x8f );
    }


    // Setup IDT Pointer
    IDTPtr.Limit    = INTTBL_SIZE * INTENTRY_SIZE - 1;
    IDTPtr.BaseAddr = (__u32)IDTTable;


    // Load IDTR 
    __asm__ __volatile__ (  \
        "lidt   (%0)\n"     \
        :: "g" (&IDTPtr)    \
    );


    // Disable all external HW interrupt sources
    ia32_IntInitPIC();


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
// ia32_IntInitPIC
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Initialize 8259a programmable interrupt controller
//
void ia32_IntInitPIC( void ) {

    // Disable all HW interrupt before any driver hook
    ia32_IoOutByte( 0xff, PIC1_REG1 );    
    ia32_IoOutByte( 0xff, PIC2_REG1 );
}


//
// ia32_IntRegisterIRQ
//
// Intput:
//  num     : IRQ number
//  handler : offset address of interrupt handler
//
// Return:
//  0  -- success
//  -1 -- failed
//
// Description:
//  Public routine for HW IRQ register
//
__u8 ia32_IntRegisterIRQ( __u8 num, __u32 handler ) {

    __u8 base = IRQ1_START;


    if( num > 15 ) {

        return -1;
    }

    if( num > 7 ) {
    
        base = IRQ2_START;
    }


    ia32_IntDisable();
    ia32_IntSetIDT( num + base, handler, (__u16)__code_seg, 0x8e );
    ia32_IntEnableIRQ( num );
    ia32_IntEnable();
    return 0;
}


//
// ia32_IntUnregisterIRQ
//
// Input:
//  num     : IRQ number
//
// Return:
//  0  -- success
//  -1 -- failed
//
// Description:
//  Public interface routine for HW IRQ unregister
//
//
__u8 ia32_IntUnregisterIRQ( __u8 num ) {

    __u8 base = IRQ1_START;


    if( num > 15 ) {
    
        return -1;
    }

    if( num > 7 ) {

        base = IRQ2_START;
    }

    ia32_IntDisable();
    ia32_IntDelIDT( num + base );
    ia32_IntDisableIRQ( num );
    ia32_IntEnable();
    return 0;
}


//
// ia32_IntEnableIRQ
//
// Input:
//  num     : IRQ number
//
// Output:
//  None
//
// Description:
//  Enable IRQ in 8259A controller
//
void ia32_IntEnableIRQ( __u8 num ) {

    __u8 reg = PIC1_REG1;

#ifdef DEBUG
    ia32_TcPrint( "Enable irq 0x%x", num );
#endif
    if( num > 7 ) {
    
        reg = PIC2_REG1;
        num -= 8;
    }

#ifdef DEBUG
    ia32_TcPrint( ", reg 0x%x, value %x", reg,  ~(1 << num) );
#endif

    ia32_IoOutByte( (ia32_IoInByte( reg ) & ~(1 << num)), reg );

#ifdef DEBUG
    ia32_TcPrint( ", result 0x%x\n", ia32_IoInByte( reg ) );
#endif
}


//
// ia32_IntDisableIRQ
//
// Input:
//  num     : IRQ number
//
// Output:
//  None
//
// Description:
//  Disable IRQ in 8259A controller
//
void ia32_IntDisableIRQ( __u8 num ) {

    __u8 reg = PIC1_REG1;

#ifdef DEBUG
    ia32_TcPrint( "Disable irq 0x%x\n", num );
#endif

    if( num > 7 ) {
    
        reg = PIC2_REG1;
        num -= 8;
    }

    ia32_IoOutByte( (ia32_IoInByte( reg ) | (1 << num)), reg );
}


