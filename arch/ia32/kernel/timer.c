/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
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
#include <ia32/i8259.h>
#include <ia32/debug.h>


//
// ia32_TmIntTimer
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Initialize 8253 timer chip
//
void ia32_TmInitTimer( void ) {

    ia32_IntDisable();
    ia32_IntRegIRQ( 0, (__u32)ia32_InterruptHandler );
    ia32_IntEnable();
}


//
// ia32_TmIntHandler
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  8253 timer interrupt handler
//
void ia32_TmIntHandler( void ) {

    __u8 volatile *videomem = (__u8 *)0xb84fe;


    //if( !(ticks % 0xffff) ) {

        (*videomem)++;
        (*(videomem + 1))++;
    //}

    ia32_IoOutByte( 0x20, 0x20 );
}


