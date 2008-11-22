/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: krn.c
 * Description:
 * 	OluxOS IA32 kernel entry point
 *
 */
#include <version.h>
#include <types.h>
#include <clib.h>
#include <ia32/platform.h>
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
#include <driver/ksh.h>
#include <fs/fat.h>
#include <ia32/gdb.h>



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


#ifdef CONFIG_SERIAL
    // Initialize Serial Port
    //SrInit();
    //ScPrint( COPYRIGHT_STR"\n" );
    //ScPrint( PRODUCT_NAME" version "KRN_VER"\n\n" );
#endif


    // Clear screen and print welcome message
    TcClear();
    TcPrint( COPYRIGHT_STR"\n" );
    TcPrint( PRODUCT_NAME" version "KRN_VER"\n\n" );


    // Check SMBIOS
    ChkSMBIOSSup();


    // Setup Page
    MmPageInit();


    // Init CPU interrupt and i8259A
    IntInitInterrupt();


    // Initialized task
    //TskInit();
    //TskStart();


	// Init timer
	TmInitTimer();
	

    // Init keyboard
    KbdInitKeyboard();


	// Enable serial interrupt
	//SrInitInterrupt();


    // Start task scheduler
    //TskScheduler();
    

    // FAT file system
    //FsFatInit();
	

	// Initialize GDB
	//GdbInit();


    KshStart();
}



