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
#include <types.h>
#include <ia32/interrupt.h>
#include <ia32/timer.h>
#include <ia32/io.h>
#include <ia32/task.h>
#include <ia32/debug.h>


extern void PreliminaryInterruptHandler_0( void );


//
// TmIntTimer
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
void TmInitTimer( void ) {

    IntDisable();
    IntRegIRQ( 0, IRQHandler( 0 ), TmIntHandler );
    IntEnable();
}


//
// TmIntHandler
//
// Input:
//  irqnum  : IRQ number
//
// Return:
//  None
//
// Description:
//  8253 timer interrupt handler
//
void TmIntHandler( __u8 irqnum ) {

    __u8 volatile *videomem = (__u8 *)0xb84fe;


    //if( !(ticks % 0xffff) ) {
        (*videomem)++;
        (*(videomem + 1))++;
    //}

    IntIssueEOI();
}


