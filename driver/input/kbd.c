/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * kbd.c -- OluxOS IA32 keyboard routines
 *
 */
#include <types.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/debug.h>
#include <driver/kbd.h>
#include <driver/console.h>


extern void PreliminaryInterruptHandler_1( void );
static __u8 CapsLock = 0;
static __u8 NumLock = 0;
static __u8 ScrollLock = 0;


//
// KbInitKeyboard
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Initialize onboard 8042 and keyboard controller
//
void KbInitKeyboard( void ) {

    IntDisable();
    IntRegIRQ( 1, IRQHandler( 1 ), KbIntHandler );
    IntEnable();
}


//
// KbIntHandler
//
// Input:
//  irqnum  : IRQ number
//
// Return:
//  None
//
// Description:
//  Keyboard interrupt handler
//
void KbIntHandler( __u8 irqnum ) {

    __u8 keycode;
    //__u8 volatile *videomem = (__u8 *)0xb831e;


    // Disable interrupt
    IntDisable();


    // Read key code
    keycode = IoInByte( 0x60 );
    if( keycode & 0x80 ) {
    
        goto KbIntHandler_Done;         
    }


    switch( keycode ) {
    
        // CapsLock
        case 0x3a :
            KbSendCmd( 0xed );
            KbSendCmd( 0x04 );
            CapsLock = ~(CapsLock & 0x01);
            break;

        // NumLock
        case 0x45 :
            KbSendCmd( 0xed );
            KbSendCmd( 0x02 );
            NumLock = ~(NumLock & 0x01 );

        // ScrollLock
        case 0x46 :
            KbSendCmd( 0xed );
            KbSendCmd( 0x01 );
            ScrollLock = ~(ScrollLock & 0x01 );
            break;

        default:
            goto KbIntHandler_Done;
    }


KbIntHandler_Done:

    //(*videomem)++;
    //(*(videomem + 1))++;
    DbgPrint( "Key code: 0x%x\n", IoInByte( 0x60 ) );

    IoOutByte( 0x20, 0x20 );

    IntEnable();
}


//
// Kb8042SendCmd
//
// Input:
//  cmd     : Command of onboard 8042 controller
//
// Return:
//  None
//
// Description:
//  Send command to onboard 8042 controller
//
void Kb8042SendCmd( __u8 cmd ) {

    // Wait for input buffer empty
    while( IoInByte( KB8042_PORT ) & 0x02 );

    IoOutByte( cmd, KB8042_PORT );

    // Wait for pc 8042 controller
    while( IoInByte( KB8042_PORT ) & 0x02 );
}


//
// KbSendCmd
//
// Input:
//  cmd     : Command of keyboard controller
//
// Return:
//  None
//
// Description:
//  Send command to keyboard controller
//
void KbSendCmd( __u8 cmd ) {

    // Disable keyboard
    Kb8042SendCmd( 0xad );

    // Send command to keyboard controller
    IoOutByte( cmd, KBCNT_PORT );

    // Reenable keyboard
    Kb8042SendCmd( 0xae );
}


