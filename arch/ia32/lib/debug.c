/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * debug.c -- OluxOS IA32 debug routines
 *
 */
#include <ia32/types.h>
#include <ia32/debug.h>
#include <ia32/console.h>


static ia32_DbgRegs_t ia32_DbgRegs;


//
// ia32_DbgSaveRegs
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
#define ia32_DbgSaveRegs()      \
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
    : "=m" (ia32_DbgRegs.eax),  \
      "=m" (ia32_DbgRegs.ebx),  \
      "=m" (ia32_DbgRegs.ecx),  \
      "=m" (ia32_DbgRegs.edx),  \
      "=m" (ia32_DbgRegs.esi),  \
      "=m" (ia32_DbgRegs.edi),  \
      "=m" (ia32_DbgRegs.ebp),  \
      "=m" (ia32_DbgRegs.esp),  \
      "=m" (ia32_DbgRegs.cr0),  \
      "=m" (ia32_DbgRegs.cr2),  \
      "=m" (ia32_DbgRegs.cr3),  \
      "=m" (ia32_DbgRegs.cr4),  \
      "=m" (ia32_DbgRegs.efl),  \
      "=m" (ia32_DbgRegs.cs),   \
      "=m" (ia32_DbgRegs.ds),   \
      "=m" (ia32_DbgRegs.es),   \
      "=m" (ia32_DbgRegs.fs),   \
      "=m" (ia32_DbgRegs.gs),   \
      "=m" (ia32_DbgRegs.ss),   \
      "=m" (ia32_DbgRegs.gdt),  \
      "=m" (ia32_DbgRegs.idt),  \
      "=m" (ia32_DbgRegs.ldt)   \
    :: "eax"                    \
    )


//
// ia32_DbgDumpRegs
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
void ia32_DbgDumpRegs( void ) {


    ia32_DbgSaveRegs();
    ia32_TcPrint( "\n"
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
                    , ia32_DbgRegs.eax
                    , ia32_DbgRegs.ebx
                    , ia32_DbgRegs.ecx
                    , ia32_DbgRegs.edx
                    , ia32_DbgRegs.esi 
                    , ia32_DbgRegs.edi 
                    , ia32_DbgRegs.ebp 
                    , ia32_DbgRegs.esp
                    , ia32_DbgRegs.cr0
                    , ia32_DbgRegs.cr2
                    , ia32_DbgRegs.cr3
                    , ia32_DbgRegs.cr4
                    , ia32_DbgRegs.cs
                    , ia32_DbgRegs.ds
                    , ia32_DbgRegs.es
                    , ia32_DbgRegs.fs
                    , ia32_DbgRegs.gs
                    , ia32_DbgRegs.ss
                    , ia32_DbgRegs.efl
                    , (__u32)((ia32_DbgRegs.gdt >> 16) & 0xffffffff)
                    , (__u32)(ia32_DbgRegs.gdt & 0xffff)
                    , (__u32)((ia32_DbgRegs.ldt >> 16) & 0xffffffff)
                    , (__u16)(ia32_DbgRegs.ldt & 0xffff)
                    , (__u32)((ia32_DbgRegs.idt >> 16) & 0xffffffff)
                    , (__u16)(ia32_DbgRegs.idt & 0xffff) );

}


