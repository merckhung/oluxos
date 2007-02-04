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
#include <ia32/types.h>
#include <ia32/console.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/kbd.h>
#include <ia32/debug.h>


extern void ia32_PreliminaryInterruptHandler_1( void );
static __u8 CapsLock = 0;
static __u8 NumLock = 0;
static __u8 ScrollLock = 0;


//
// ia32_KbInitKeyboard
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
void ia32_KbInitKeyboard( void ) {

    ia32_IntDisable();
    ia32_IntRegIRQ( 1, IRQHandler( 1 ), ia32_KbIntHandler );
    ia32_IntEnable();
}


//
// ia32_KbIntHandler
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
void ia32_KbIntHandler( __u8 irqnum ) {

    __u8 keycode;
    //__u8 volatile *videomem = (__u8 *)0xb831e;


    // Disable interrupt
    ia32_IntDisable();


    // Read key code
    keycode = ia32_IoInByte( 0x60 );
    if( keycode & 0x80 ) {
    
        goto ia32_KbIntHandler_Done;         
    }


    switch( keycode ) {
    
        // CapsLock
        case 0x3a :
            ia32_KbSendCmd( 0xed );
            ia32_KbSendCmd( 0x04 );
            CapsLock = ~(CapsLock & 0x01);
            break;

        // NumLock
        case 0x45 :
            ia32_KbSendCmd( 0xed );
            ia32_KbSendCmd( 0x02 );
            NumLock = ~(NumLock & 0x01 );

        // ScrollLock
        case 0x46 :
            ia32_KbSendCmd( 0xed );
            ia32_KbSendCmd( 0x01 );
            ScrollLock = ~(ScrollLock & 0x01 );
            break;

        default:
            goto ia32_KbIntHandler_Done;
    }


ia32_KbIntHandler_Done:

    //(*videomem)++;
    //(*(videomem + 1))++;
    //ia32_TcPrint( "Key code: 0x%x\n", ia32_IoInByte( 0x60 ) );

    ia32_IoOutByte( 0x20, 0x20 );

    ia32_IntEnable();
}


//
// ia32_Kb8042SendCmd
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
void ia32_Kb8042SendCmd( __u8 cmd ) {

    // Wait for input buffer empty
    while( ia32_IoInByte( KB8042_PORT ) & 0x02 );

    ia32_IoOutByte( cmd, KB8042_PORT );

    // Wait for pc 8042 controller
    while( ia32_IoInByte( KB8042_PORT ) & 0x02 );
}


//
// ia32_KbSendCmd
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
void ia32_KbSendCmd( __u8 cmd ) {

    // Disable keyboard
    ia32_Kb8042SendCmd( 0xad );

    // Send command to keyboard controller
    ia32_IoOutByte( cmd, KBCNT_PORT );

    // Reenable keyboard
    ia32_Kb8042SendCmd( 0xae );
}


