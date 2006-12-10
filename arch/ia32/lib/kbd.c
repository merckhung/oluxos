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


extern void _ia32_KbIntHandler( void );


void ia32_KbInitKeyboard( void ) {

    ia32_IntDisable();

    ia32_KbSendCmd( 0xed );
    ia32_KbSendCmd( 0x07 );

    ia32_IntRegisterIRQ( 1, (__u32)_ia32_KbIntHandler );

    ia32_IntEnable();
}


void ia32_KbIntHandler( void ) {

    __u8 volatile *videomem = (__u8 *)0xb831e;


    ia32_IntDisable();

    ia32_TcPrint( "Keyboard\n" );

    (*videomem)++;
    (*(videomem + 1))++;
    ia32_IoOutByte( 0x20, PIC1_REG0 );

    ia32_IntEnable();
}


void ia32_Kb8042SendCmd( __u8 cmd ) {

    // Wait for input buffer empty
    while( ia32_IoInByte( KB8042_PORT ) & 0x02 );

    ia32_IoOutByte( cmd, KB8042_PORT );

    // Wait for pc 8042 controller
    while( ia32_IoInByte( KB8042_PORT ) & 0x02 );
}


void ia32_KbSendCmd( __u8 cmd ) {

    // Disable keyboard
    ia32_Kb8042SendCmd( 0xad );

    // Send command to keyboard controller
    ia32_IoOutByte( cmd, KBCNT_PORT );

    // Reenable keyboard
    ia32_Kb8042SendCmd( 0xae );
}


