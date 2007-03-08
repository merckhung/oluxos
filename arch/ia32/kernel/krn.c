/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * krn.c -- OluxOS IA32 kernel entry point
 *
 */
#include <types.h>
#include <ia32/page.h>
#include <ia32/task.h>
#include <ia32/interrupt.h>
#include <ia32/timer.h>
#include <ia32/debug.h>
#include <driver/console.h>
#include <driver/kbd.h>
#include <driver/pci.h>
#include <driver/ide.h>
#include <driver/resource.h>
#include <clib.h>


static struct IRQStack_t IRQStack;


//
// krn_entry
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
void krn_entry( void ) {


    // Clear screen and print welcome message
    TcClear();
    TcPrint( "Copyright (C) 2006 - 2007 Olux Organization all rights reserved.\n" );
    TcPrint( "Olux Operating System v0.1\n\n" );


    // Setup IRQ stack
    memset( &IRQStack.stack, 0, SZ_IRQ_STACK );
    IRQStack.esp = (__u32)(IRQStack.stack + SZ_IRQ_STACK);
    __asm__ __volatile__ (
    
        "movl   %0, %%esp\n"
        :: "g" (IRQStack.esp)
    );


    // Check SMBIOS
    ChkSMBIOSSup();


    // Setup Page
    MmPageInit();


    // Init CPU interrupt and i8259A
    IntInitInterrupt();


    // Scan Pci
    PciDetectDevice();


    // Initialized task
    //TskInit();


    // Initialize IDE Hard Disk
    IDEInit();

 
    // Init keyboard
    KbdInitKeyboard();


    // Init timer
    TmInitTimer();


    // Start task scheduler
    //TskScheduler();


    for(;;);
}


