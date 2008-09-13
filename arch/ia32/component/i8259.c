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


    DbgPrint( "Disable all IRQ for initial processes\n" );
    // Mask all HW interrupt for initialization
    IoOutByte( 0xFF, PIC_MASTER_IMR );    
    IoOutByte( 0xFF, PIC_SLAVE_IMR );



    DbgPrint( "Initialize Master PIC\n" );
    //
    // Initialize Master PIC
    //
    // 1. Let maste and slave PIC go into initialize sequence
    //    D4 = 1, D0 = 1 : 0x11
    // 2. Remap IRQ0 to Interrupt 32
    // 3. Slave PIC connected at IRQ2, Interrupt 33
    // 4. Set to microprocessor and normal EOI mode
    //
    IoOutByte( 0x11, PIC_MASTER_ICW1 );
    IoOutByte( PIC_IRQ_BASE, PIC_MASTER_ICW2 );
    IoOutByte( 1U << PIC_CASCADE_IR, PIC_MASTER_ICW3 );
    IoOutByte( 0x01, PIC_MASTER_ICW4 );


    DbgPrint( "Initialize Slave PIC\n" );
    //
    // Initialize Slave PIC
    //
    // Note: remap IRQ8 to IRQ40
    //
    IoOutByte( 0x11, PIC_SLAVE_ICW1 );
    IoOutByte( PIC_IRQ_SLAVE, PIC_SLAVE_ICW2 );
    IoOutByte( 1U << PIC_CASCADE_IR, PIC_SLAVE_ICW3 );
    IoOutByte( 0x01, PIC_SLAVE_ICW4 );


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
void i8259EnableIRQ( u8 irqnum ) {

    u8 reg = PIC_MASTER_IMR;

    if( irqnum & 0x8 ) {
    
        reg = PIC_SLAVE_IMR; 
        irqnum &= 0x7;
    }
    IoOutByte( (IoInByte( reg ) & ~(1 << irqnum)), reg );
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
void i8259DisableIRQ( u8 irqnum ) {

    u8 reg = PIC_MASTER_IMR;

    if( irqnum & 0x8 ) {
    
        reg = PIC_SLAVE_IMR;   
        irqnum &= 0x7;
    }
    IoOutByte( (IoInByte( reg ) | (1 << irqnum)), reg );
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


