/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


typedef struct {

    __u32   cr3;
    __u32   eip;
    __u32   eflags;
    __u32   eax;
    __u32   ecx;
    __u32   edx;
    __u32   ebx;
    __u32   esp;
    __u32   ebp;
    __u32   esi;
    __u32   edi;
    __u16   es;
    __u16   cs;
    __u16   ss;
    __u16   ds;
    __u16   fs;
    __u16   gs;
} Task_t;


typedef struct {

    __u16   prev_task_link;
    __u16   reserved0;
    __u32   esp0;
    __u16   ss0;
    __u16   reserved1;
    __u32   esp1;
    __u16   ss1;
    __u16   reserved2;
    __u32   esp2;
    __u16   ss2;
    __u16   reserved3;
    __u32   cr3;
    __u32   eip;
    __u32   eflags;
    __u32   eax;
    __u32   ecx;
    __u32   edx;
    __u32   ebx;
    __u32   esp;
    __u32   ebp;
    __u32   esi;
    __u32   edi;
    __u16   es;
    __u16   reserved4;
    __u16   cs;
    __u16   reserved5;
    __u16   ss;
    __u16   reserved6;
    __u16   ds;
    __u16   reserved7;
    __u16   fs;
    __u16   reserved8;
    __u16   gs;
    __u16   reserved9;
    __u16   ldt;
    __u16   reserved10;
    __u16   debug;
    __u16   iobmp;
} __attribute__ ((packed)) TSS_t;


typedef struct {

    __u16   limit0;
    __u16   baseaddr0;
    __u8    baseaddr1;
    __u8    flag;
    __u8    limit1;
    __u8    baseaddr2;
} __attribute__ ((packed)) TSSD_t;


void TskInit( void );
void TskStart( void );
void TskSwitch( __u32 origtsk, __u32 newtsk );
void TskScheduler( void );


