/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * ia32_krn.c -- OluxOS IA32 kernel entry point
 *
 */
#include <ia32/types.h>
#include <ia32/console.h>
#include <ia32/page.h>
#include <ia32/pci.h>
#include <ia32/interrupt.h>
#include <ia32/timer.h>
#include <ia32/kbd.h>
#include <ia32/debug.h>
#include <ia32/resource.h>


//
// ia32_krn_entry
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  This is IA32 kerne entry point
//
void ia32_krn_entry( void ) {

    ia32_MmPageInit();
    ia32_TcClear();
    ia32_TcPrint( "Copyright (C) 2006 - 2007 Olux Organization all rights reserved.\n" );
    ia32_TcPrint( "Welcome to OluxOS v0.1\n\n" );

    // Init CPU interrupt and i8259A
    ia32_IntInitInterrupt();

    // Scan PCI
    ia32_PCIDetectDevice();

    // Scan resource
    ChkSMBIOSSup();

    // Init keyboard
    ia32_KbInitKeyboard();

    // Init timer
    ia32_TmInitTimer();


    for( ; ; );
}


