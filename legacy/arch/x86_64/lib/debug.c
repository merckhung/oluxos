/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * debug.c -- OluxOS IA32 debug routines
 *
 */
#include <driver/console.h>
#include <x86_64/debug.h>
#include <types.h>

#ifdef KERNEL_DEBUG
static DbgRegs_t DbgRegs;

//
// DbgSaveRegs
//
#define DbgSaveRegs()

//
// DbgDumpRegs
//
void DbgDumpRegs(void) {
  TcPrint("x86_64 DbgDumpRegs not fully implemented\n");
}
#endif
