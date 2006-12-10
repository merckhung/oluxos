/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * timer.c -- OluxOS IA32 timer routines
 *
 */
#include <ia32/types.h>
#include <ia32/console.h>
#include <ia32/interrupt.h>
#include <ia32/timer.h>
#include <ia32/io.h>
#include <ia32/debug.h>


extern __u32 ticks;


extern void _ia32_TmIntHandler( void );


void ia32_TmInitTimer( void ) {

    ia32_IntRegisterIRQ( 0, (__u32)_ia32_TmIntHandler );
}


void ia32_TmIntHandler( void ) {

    __u8 volatile *videomem = (__u8 *)0xb84fe;


    ia32_IntDisable();

    if( !(ticks % 0xffff) ) {

        (*videomem)++;
        (*(videomem + 1))++;
        ia32_IoOutByte( 0x20, PIC1_REG0 );
    }

    ia32_IntEnable();
}


