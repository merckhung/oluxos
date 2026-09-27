/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: krn.c
 * Description:
 * 	OluxOS IA32 kernel entry point
 *
 */
#include <clib.h>
#include <driver/console.h>
#include <driver/ide.h>
#include <driver/kbd.h>
#include <driver/ksh.h>
#include <driver/pci.h>
#include <driver/resource.h>
#include <driver/sercon.h>
#include <driver/serial.h>
#include <fs/fat.h>
#include <ia32/debug.h>
#include <ia32/gdb.h>
#include <ia32/interrupt.h>
#include <ia32/kdbger.h>
#include <ia32/page.h>
#include <ia32/platform.h>
#include <ia32/task.h>
#include <ia32/timer.h>
#include <types.h>
#include <version.h>

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
void krn_entry(void) {
  // Initialize Serial Port
  SrInit();
  ScPrint(COPYRIGHT_STR "\n");
  ScPrint(PRODUCT_NAME " version " KRN_VER "\n\n");

  // Clear screen and print welcome message
  TcClear();
  TcPrint(COPYRIGHT_STR "\n");
  TcPrint(PRODUCT_NAME " version " KRN_VER "\n\n");

  // Check SMBIOS
  // ChkSMBIOSSup();

  // Setup Page
  TcPrint("MmPageInit\n");
  MmPageInit();
  TcPrint("PciDetectDevice\n");
  PciDetectDevice();

  // Init CPU interrupt and i8259A
  TcPrint("IntInitInterrupt\n");
  IntInitInterrupt();
  // Initialize Kernel Debugger
  TcPrint("kdbgerInitialization\n");
  kdbgerInitialization(UART_PORT0);

  // Initialized task
  TcPrint("TskInit\n");
  // TskInit();
  TcPrint("TskStart\n");
  // TskStart();

  // Init timer
  TcPrint("TmInitTimer\n");
  TmInitTimer();

  // Init keyboard
  TcPrint("KbdInitKeyboard\n");
  KbdInitKeyboard();

  // Enable serial interrupt
  TcPrint("SrInitInterrupt\n");
  SrInitInterrupt();

  // Start task scheduler
  // TskScheduler();

  // FAT file system
  TcPrint("FsFatInit\n");
  FsFatInit();

  TcPrint("KshStart\n");
  KshStart();
  // Initialize GDB
  // GdbInit();

  KshStart();

  for (;;);
}
