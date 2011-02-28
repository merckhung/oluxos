/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * i8259.c -- Intel architecture legacy interrupt controller
 *
 */
#include <types.h>
#include <clib.h>
#include <ia32/platform.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/i8259.h>
#include <ia32/debug.h>


//
// i8259Init
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Initialize 8259A Programmable Interrupt Controller
//
void i8259Init( void ) {


    // Mask all HW interrupt for initialization
    IoOutByte( 0xFF, PIC_MASTER_IMR );    
    IoOutByte( 0xFF, PIC_SLAVE_IMR );



    DbgPrint( "Initialize Master i8259\n" );
    //
    // Initialize Master PIC
    //
    // 2. Remap IRQ0 to Interrupt 32
    // 3. Slave PIC connected at IRQ2, Interrupt 33
    // 4. Set to microprocessor and normal EOI mode
    //
	// ICW1, Edge Trigger, Cascaded, ICW4
    IoOutByte( 0x11, PIC_MASTER_CMD );

	// ICW2, Remap IRQ0 to interrupt vector 32 (0x20)
    IoOutByte( PIC_IRQ_BASE, PIC_MASTER_IMR );

	// ICW3
    IoOutByte( 1U << PIC_CASCADE_IR, PIC_MASTER_IMR );

	// ICW4, 8086/88 mode
    IoOutByte( 0x01, PIC_MASTER_IMR );


    DbgPrint( "Initialize Slave i8259\n" );
    //
    // Initialize Slave PIC
    //
    // Note: remap IRQ8 to IRQ40
    //
    IoOutByte( 0x11, PIC_SLAVE_CMD );
    IoOutByte( PIC_IRQ_SLAVE, PIC_SLAVE_IMR );
    IoOutByte( 1U << PIC_CASCADE_IR, PIC_SLAVE_IMR );
    IoOutByte( 0x01, PIC_SLAVE_IMR );


    // Mask all HW interrupt again
    IoOutByte( 0xFF, PIC_MASTER_IMR );    
    IoOutByte( 0xFF, PIC_SLAVE_IMR );
}


//
// i8259EnableIRQ
//
// Intput:
//  irqnum      : IRQ number
//
// Return:
//  0  -- success
//  -1 -- failed
//
// Description:
//  Enable i8259A IRQ line
//
void i8259EnableIRQ( u8 IrqNum ) {

    u8 reg = PIC_MASTER_IMR;


    if( IrqNum >= 8 ) {
    
        reg = PIC_SLAVE_IMR; 
        IrqNum &= 0x7;
    }


    IoOutByte( IoInByte( reg ) & ~(1 << IrqNum), reg );
}


//
// i8259DisableIRQ
//
// Input:
//  irqnum      : IRQ number
//
// Output:
//  None
//
// Description:
//  Disable i8259A IRQ line
//
void i8259DisableIRQ( u8 IrqNum ) {

    u8 reg = PIC_MASTER_IMR;


    if( IrqNum & 0x8 ) {
    
        reg = PIC_SLAVE_IMR;   
        IrqNum &= 0x7;
    }

	
    IoOutByte( IoInByte( reg ) | (1 << IrqNum), reg );
}


//
// i8259DisableIRQ
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
void i8259IssueEOI( void ) {

    IoOutByte( 0x20, PIC_MASTER_CMD );
}


