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
#include <types.h>
#include <ia32/debug.h>
#include <driver/console.h>


static DbgRegs_t DbgRegs;


//
// DbgSaveRegs
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Save all registers in global variables
//
#define DbgSaveRegs()      \
__asm__ __volatile__ (          \
    "movl   %%eax, %0\n"        \
    "movl   %%ebx, %1\n"        \
    "movl   %%ecx, %2\n"        \
    "movl   %%edx, %3\n"        \
    "movl   %%esi, %4\n"        \
    "movl   %%edi, %5\n"        \
    "movl   %%ebp, %6\n"        \
    "movl   %%esp, %7\n"        \
    "movl   %%cr0, %%eax\n"     \
    "movl   %%eax, %8\n"        \
    "movl   %%cr2, %%eax\n"     \
    "movl   %%eax, %9\n"        \
    "movl   %%cr3, %%eax\n"     \
    "movl   %%eax, %10\n"       \
    "movl   %%cr4, %%eax\n"     \
    "movl   %%eax, %11\n"       \
    "pushf\n"                   \
    "popl   %%eax\n"            \
    "mov    %%eax, %12\n"       \
    "mov    %%cs, %%ax\n"       \
    "mov    %%ax, %13\n"        \
    "mov    %%ds, %%ax\n"       \
    "mov    %%ax, %14\n"        \
    "mov    %%es, %%ax\n"       \
    "mov    %%ax, %15\n"        \
    "mov    %%fs, %%ax\n"       \
    "mov    %%ax, %16\n"        \
    "mov    %%gs, %%ax\n"       \
    "mov    %%ax, %17\n"        \
    "mov    %%ss, %%ax\n"       \
    "mov    %%ax, %18\n"        \
    "sgdt   %19\n"              \
    "sidt   %20\n"              \
    "sldt   %21\n"              \
    : "=m" (DbgRegs.eax),  \
      "=m" (DbgRegs.ebx),  \
      "=m" (DbgRegs.ecx),  \
      "=m" (DbgRegs.edx),  \
      "=m" (DbgRegs.esi),  \
      "=m" (DbgRegs.edi),  \
      "=m" (DbgRegs.ebp),  \
      "=m" (DbgRegs.esp),  \
      "=m" (DbgRegs.cr0),  \
      "=m" (DbgRegs.cr2),  \
      "=m" (DbgRegs.cr3),  \
      "=m" (DbgRegs.cr4),  \
      "=m" (DbgRegs.efl),  \
      "=m" (DbgRegs.cs),   \
      "=m" (DbgRegs.ds),   \
      "=m" (DbgRegs.es),   \
      "=m" (DbgRegs.fs),   \
      "=m" (DbgRegs.gs),   \
      "=m" (DbgRegs.ss),   \
      "=m" (DbgRegs.gdt),  \
      "=m" (DbgRegs.idt),  \
      "=m" (DbgRegs.ldt)   \
    :: "eax"                    \
    )


//
// DbgDumpRegs
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Print all register values from global variables
//
void DbgDumpRegs( void ) {


    DbgSaveRegs();
    TcPrint( "\n"
                  "General Purpose Registers:\n"
                  "EAX = %8X, EBX = %8X, ECX = %8X, EDX = %8X\n"
                  "ESI = %8X, EDI = %8X, EBP = %8X, ESP = %8X\n\n"
                  "Control Registers:\n"
                  "CR0 = %8X, CR2 = %8X, CR3 = %8X, CR4 = %8X\n\n"
                  "Segment & Flags Registers:\n"
                  "CS = %4X, DS = %4X, ES = %4X, FS = %4X\n"
                  "GS = %4X, SS = %4X, EFL = %8X\n\n"
                  "Descriptor Registers:\n"
                  "GDT Base = %8X, GDT Limit = %4X\n"
                  "LDT Base = %8X, LDT Limit = %4X\n"
                  "IDT Base = %8X, IDT Limit = %4X\n"
                    , DbgRegs.eax
                    , DbgRegs.ebx
                    , DbgRegs.ecx
                    , DbgRegs.edx
                    , DbgRegs.esi 
                    , DbgRegs.edi 
                    , DbgRegs.ebp 
                    , DbgRegs.esp
                    , DbgRegs.cr0
                    , DbgRegs.cr2
                    , DbgRegs.cr3
                    , DbgRegs.cr4
                    , DbgRegs.cs
                    , DbgRegs.ds
                    , DbgRegs.es
                    , DbgRegs.fs
                    , DbgRegs.gs
                    , DbgRegs.ss
                    , DbgRegs.efl
                    , (__u32)((DbgRegs.gdt >> 16) & 0xffffffff)
                    , (__u32)(DbgRegs.gdt & 0xffff)
                    , (__u32)((DbgRegs.ldt >> 16) & 0xffffffff)
                    , (__u16)(DbgRegs.ldt & 0xffff)
                    , (__u32)((DbgRegs.idt >> 16) & 0xffffffff)
                    , (__u16)(DbgRegs.idt & 0xffff) );

}


