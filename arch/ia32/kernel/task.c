/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * task.c -- OluxOS IA32 task routines
 *
 */
#include <types.h>
#include <driver/console.h>
#include <ia32/debug.h>
#include <ia32/task.h>
#include <ia32/interrupt.h>
#include <clib.h>


static TSS_t KrnTSS;

static Task_t tsks[ 2 ];
static char stack_buf[ 2 ][ 1024 ];

extern void __task_seg( void );
extern void KrnTSSD( void );

static __u8 swh_time = 0;
static __u32 test = 0;


void TskTest1( void ) {

    __u32 i, j;

    for( i = 0, j = 0 ; ; i++ ) {
    
        if( !(i % 90000000) ) {

            DbgPrint( "Task1: %d\n", j );
            j++;
        }
    }
}


void TskTest2( void ) {

    __u32 i, j;

    for( i = 0, j = 0 ; ; i++ ) {
    
        if( !(i % 50000000) ) {

            DbgPrint( "Task2: %d\n", j );
            j++;
        }
    }
}


void TskInit( void ) {

    TSSD_t *p = (TSSD_t *)KrnTSSD;


    //
    // Override TSSD default value
    //
    p->limit0       = (0x0000ffff & sizeof( TSS_t ));
    p->limit1       = (0x000f0000 & sizeof( TSS_t )) >> 16;
    p->baseaddr0    = (0x0000ffff & (__u32)&KrnTSS);
    p->baseaddr1    = (0x00ff0000 & (__u32)&KrnTSS) >> 16;
    p->baseaddr2    = (0xff000000 & (__u32)&KrnTSS) >> 24;
    p->flag         = 0x89;


    //
    // Load Kernel TSS Descriptor
    //
    __asm__ __volatile__ (

        "mov    %0, %%ax\n"
        "ltr    %%ax\n"
        :: "g" (__task_seg)
        : "ax"
    );


    //
    // Fill up Task descriptors
    //
    tsks[ 0 ].eip       = (__u32)TskTest1;
    tsks[ 0 ].cs        = 0x10;
    tsks[ 0 ].ss        = 0x18;
    tsks[ 0 ].esp       = (__u32)stack_buf[ 0 ] + 1024;
    tsks[ 0 ].eflags    = 0x200;

    tsks[ 1 ].eip       = (__u32)TskTest2;
    tsks[ 1 ].cs        = 0x10;
    tsks[ 1 ].ss        = 0x18;
    tsks[ 1 ].esp       = (__u32)stack_buf[ 1 ] + 1024;
    tsks[ 1 ].eflags    = 0x200;
}


void TskSwitch( void ) {

    swh_time++;


    //
    // Switch to Task1
    //
        __asm__ __volatile__ (

            "movl   %0, %%eax\n"
            "movl   %1, %%ecx\n"
            "movl   %2, %%edx\n"
            "movl   %3, %%ebx\n"
            "movl   %4, %%ebp\n"
            "movl   %5, %%esi\n"
            "movl   %6, %%edi\n"
            "pushl  %7\n"
            "popf\n"
            "movl   %8, %%esp\n"
            "jmp    *(%9)\n"
            : "=m" (tsks[ 0 ].eax),
              "=m" (tsks[ 0 ].ecx),
              "=m" (tsks[ 0 ].edx),
              "=m" (tsks[ 0 ].ebx),
              "=m" (tsks[ 0 ].ebp),
              "=m" (tsks[ 0 ].esi),
              "=m" (tsks[ 0 ].edi),
              "=m" (tsks[ 0 ].eflags),
              "=m" (tsks[ 0 ].esp),
              "=m" (tsks[ 0 ].eip)
        );
}


void TskScheduler( void ) {

    TskSwitch();
    for(;;);
}


void TskReschedule( struct SavedRegs_t regs ) {


#if 0
        DbgPrint( "Current Process:\n" );
        DbgPrint( "EAX = %8x, EBX = %8x, ECX = %8x, EDX = %8x\n"
                , regs.eax, regs.ebx, regs.ecx, regs.edx );
        DbgPrint( "ESI = %8x, EDI = %8x, EBP = %8x, EIP = %8x\n"
                , regs.esi, regs.edi, regs.ebp, regs.eip );
        DbgPrint( "CS = %8x, EFLAG = %8x\n", regs.cs, regs.eflags );
#endif
       

        tsks[ test % 2 ].eax = regs.eax;
        tsks[ test % 2 ].ebx = regs.ebx;
        tsks[ test % 2 ].ecx = regs.ecx;
        tsks[ test % 2 ].edx = regs.edx;
        tsks[ test % 2 ].esi = regs.esi;
        tsks[ test % 2 ].edi = regs.edi;
        tsks[ test % 2 ].ebp = regs.ebp;
        tsks[ test % 2 ].eip = regs.eip;
        tsks[ test % 2 ].eflags = regs.eflags;

        regs.eax = tsks[ (test + 1) % 2 ].eax;
        regs.ebx = tsks[ (test + 1) % 2 ].ebx;
        regs.ecx = tsks[ (test + 1) % 2 ].ecx;
        regs.edx = tsks[ (test + 1) % 2 ].edx;
        regs.esi = tsks[ (test + 1) % 2 ].esi;
        regs.edi = tsks[ (test + 1) % 2 ].edi;
        regs.ebp = tsks[ (test + 1) % 2 ].ebp;
        regs.eip = tsks[ (test + 1) % 2 ].eip;
        regs.eflags = tsks[ (test + 1) % 2 ].eflags;
        

#if 0
        DbgPrint( "Resume Process:\n" );
        DbgPrint( "EAX = %8x, EBX = %8x, ECX = %8x, EDX = %8x\n"
                , regs.eax, regs.ebx, regs.ecx, regs.edx );
        DbgPrint( "ESI = %8x, EDI = %8x, EBP = %8x, EIP = %8x\n"
                , regs.esi, regs.edi, regs.ebp, regs.eip );
        DbgPrint( "CS = %8x, EFLAG = %8x\n", regs.cs, regs.eflags );
#endif


        test++;
}


