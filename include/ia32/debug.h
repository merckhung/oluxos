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

    __u32 eax;
    __u32 ebx;
    __u32 ecx;
    __u32 edx;

    __u32 esi;
    __u32 edi;
    __u32 ebp;
    __u32 esp;

    __u32 efl;

    __u16 cs;
    __u16 ds;
    __u16 es;
    __u16 fs;
    __u16 gs;
    __u16 ss;

    __u64 gdt;

    __u64 ldt;

    __u64 idt;

    __u32 cr0;
    __u32 cr2;
    __u32 cr3;
    __u32 cr4;

} DbgRegs_t;


void DbgDumpRegs( void );
#endif


