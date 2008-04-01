/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */
#ifdef KERNEL_DEBUG
#include <driver/console.h>
#endif


#ifdef KERNEL_DEBUG
#define DbgPrint( msg, args... )    TcPrint( msg, ##args );
#else
#define DbgPrint( msg, args... )
#endif


#ifdef KERNEL_DEBUG
typedef struct {

    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;

    u32 esi;
    u32 edi;
    u32 ebp;
    u32 esp;

    u32 efl;

    u16 cs;
    u16 ds;
    u16 es;
    u16 fs;
    u16 gs;
    u16 ss;

    u64 gdt;

    u64 ldt;

    u64 idt;

    u32 cr0;
    u32 cr2;
    u32 cr3;
    u32 cr4;

} DbgRegs_t;


void DbgDumpRegs( void );
#endif


