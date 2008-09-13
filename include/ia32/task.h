/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


typedef struct {

    u32   cr3;
    u32   eip;
    u32   eflags;
    u32   eax;
    u32   ecx;
    u32   edx;
    u32   ebx;
    u32   esp;
    u32   ebp;
    u32   esi;
    u32   edi;
    u16   es;
    u16   cs;
    u16   ss;
    u16   ds;
    u16   fs;
    u16   gs;
} Task_t;


typedef struct {

    u16   prev_task_link;
    u16   reserved0;
    u32   esp0;
    u16   ss0;
    u16   reserved1;
    u32   esp1;
    u16   ss1;
    u16   reserved2;
    u32   esp2;
    u16   ss2;
    u16   reserved3;
    u32   cr3;
    u32   eip;
    u32   eflags;
    u32   eax;
    u32   ecx;
    u32   edx;
    u32   ebx;
    u32   esp;
    u32   ebp;
    u32   esi;
    u32   edi;
    u16   es;
    u16   reserved4;
    u16   cs;
    u16   reserved5;
    u16   ss;
    u16   reserved6;
    u16   ds;
    u16   reserved7;
    u16   fs;
    u16   reserved8;
    u16   gs;
    u16   reserved9;
    u16   ldt;
    u16   reserved10;
    u16   debug;
    u16   iobmp;
} __attribute__ ((packed)) TSS_t;


typedef struct {

    u16   limit0;
    u16   baseaddr0;
    u8    baseaddr1;
    u8    flag;
    u8    limit1;
    u8    baseaddr2;
} __attribute__ ((packed)) TSSD_t;


void TskInit( void );
void TskStart( void );
void TskSwitch( u32 origtsk, u32 newtsk );
void TskScheduler( void );


