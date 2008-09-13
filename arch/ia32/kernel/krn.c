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
#include <version.h>
#include <types.h>
#include <clib.h>
#include <ia32/page.h>
#include <ia32/task.h>
#include <ia32/interrupt.h>
#include <ia32/timer.h>
#include <ia32/debug.h>
#include <driver/console.h>
#include <driver/kbd.h>
#include <driver/pci.h>
#include <driver/resource.h>
#include <driver/ide.h>
#include <driver/serial.h>
#include <driver/sercon.h>
#include <fs/fat.h>
#include <ia32/gdb.h>



//static struct IRQStack_t IRQStack;


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


    // Initialize Serial Port
    SrInit();
    ScPrint( COPYRIGHT_STR"\n" );
    ScPrint( PRODUCT_NAME" version "KRN_VER"\n\n" );


    // Clear screen and print welcome message
    TcClear();
    TcPrint( COPYRIGHT_STR"\n" );
    TcPrint( PRODUCT_NAME" version "KRN_VER"\n\n" );


    // Setup IRQ stack
/*
    CbMemSet( &IRQStack.stack, 0, SZ_IRQ_STACK );
    IRQStack.esp = (u32)(IRQStack.stack + SZ_IRQ_STACK);
    __asm__ __volatile__ (
    
        "movl   %0, %%esp\n"
        :: "g" (IRQStack.esp)
    );
*/


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
    //TskStart();


    // Initialize IDE Hard Disk
    //IDEInit();
	

    // Init keyboard
    KbdInitKeyboard();


    // Init timer
    //TmInitTimer();


    // Start task scheduler
    //TskScheduler();
    

    // Start Setup Menu
    //MenuInit();
    

    // FAT file system
    //FsFatInit();


    for(;;);
}


