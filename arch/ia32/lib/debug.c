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
#include <ia32/debug.h>


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


void ia32_DbgDumpRegs( void ) {


    ia32_DbgSaveRegs();
    ia32_TcPrint( "\n"
                  "EAX = %8U, EBX = %8U, ECX = %8U, EDX = %8U\n"
                  "ESI = %8U, EDI = %8U, EBP = %8U, ESP = %8U\n\n"
                  "CR0 = %8U, CR2 = %8U, CR3 = %8U, CR4 = %8U\n\n"
                  "CS = %4U, DS = %4U, ES = %4U, FS = %4U\n"
                  "GS = %4U, SS = %4U, EFL = %8U\n\n"
                  "GDT = %8U, LDT = %8U, IDT = %8U\n"
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
                    , ia32_DbgRegs.gdt
                    , ia32_DbgRegs.ldt
                    , ia32_DbgRegs.idt );

}


