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

static __u32 current = -1;


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


void TskUsr1( void ) {

    __u32 i, j;

    for( i = 0 ; ; i++ );
        for( j = 0 ; ; j++ );
}


void TskUsr2( void ) {

    __u32 i, j;

    for( i = 0 ; ; i++ );
        for( j = 0 ; ; j++ );
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
    tsks[ 0 ].eip       = (__u32)TskUsr1;
    tsks[ 0 ].cs        = 0x10;
    tsks[ 0 ].ss        = 0x18;
    tsks[ 0 ].esp       = (__u32)stack_buf[ 0 ] + 1024;
    tsks[ 0 ].eflags    = 0x200;

    tsks[ 1 ].eip       = (__u32)TskUsr2;
    tsks[ 1 ].cs        = 0x10;
    tsks[ 1 ].ss        = 0x18;
    tsks[ 1 ].esp       = (__u32)stack_buf[ 1 ] + 1024;
    tsks[ 1 ].eflags    = 0x200;
}


void TskSwitch( __u32 origtsk, __u32 newtsk ) {


    if( origtsk >= 0 ) {
    
    
#if 0
        DbgPrint( "\nCurrent Process:\n" );
        DbgPrint( "EAX = %8x, EBX = %8x, ECX = %8x, EDX = %8x\n"
                , regs.eax, regs.ebx, regs.ecx, regs.edx );
        DbgPrint( "ESI = %8x, EDI = %8x, EBP = %8x, EIP = %8x\n"
                , regs.esi, regs.edi, regs.ebp, regs.eip );
        DbgPrint( "DS = %8x, ES = %8x, CS = %8x\n"
                , regs.ds, regs.es, regs.cs );
        DbgPrint( "EFLAGS = %8x\n", regs.eflags );
#endif


#if 0
        tsks[ origtsk ].eax = regs.eax;
        tsks[ origtsk ].ebx = regs.ebx;
        tsks[ origtsk ].ecx = regs.ecx;
        tsks[ origtsk ].edx = regs.edx;
        tsks[ origtsk ].esi = regs.esi;
        tsks[ origtsk ].edi = regs.edi;
        tsks[ origtsk ].ebp = regs.ebp;
        tsks[ origtsk ].eip = regs.eip;
        tsks[ origtsk ].eflags = regs.eflags;

        regs.eax = tsks[ newtsk ].eax;
        regs.ebx = tsks[ newtsk ].ebx;
        regs.ecx = tsks[ newtsk ].ecx;
        regs.edx = tsks[ newtsk ].edx;
        regs.esi = tsks[ newtsk ].esi;
        regs.edi = tsks[ newtsk ].edi;
        regs.ebp = tsks[ newtsk ].ebp;
        regs.eip = tsks[ newtsk ].eip;
        regs.eflags = tsks[ newtsk ].eflags;
#endif
        

#if 0
        DbgPrint( "\nResume Process:\n" );
        DbgPrint( "EAX = %8x, EBX = %8x, ECX = %8x, EDX = %8x\n"
                , regs.eax, regs.ebx, regs.ecx, regs.edx );
        DbgPrint( "ESI = %8x, EDI = %8x, EBP = %8x, EIP = %8x\n"
                , regs.esi, regs.edi, regs.ebp, regs.eip );
        DbgPrint( "DS = %8x, ES = %8x, CS = %8x\n"
                , regs.ds, regs.es, regs.cs );
        DbgPrint( "EFLAGS = %8x\n", regs.eflags );
#endif

   
    }
    else {

        //
        // Just switch to Task1
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
            "popfl\n"
            "movl   %8, %%esp\n"
            "jmp    *(%9)\n"
            :: "g" (tsks[ 0 ].eax),
              "g" (tsks[ 0 ].ecx),
              "g" (tsks[ 0 ].edx),
              "g" (tsks[ 0 ].ebx),
              "g" (tsks[ 0 ].ebp),
              "g" (tsks[ 0 ].esi),
              "g" (tsks[ 0 ].edi),
              "g" (tsks[ 0 ].eflags),
              "g" (tsks[ 0 ].esp),
              "g" (tsks[ 0 ].eip)
        );
    }

}


void TskScheduler( void ) {


    __u32 now;

    now = current;
    do {
        
        if( current >= 2 )  {
        
            current = 0;
        }
        else {
        
            current++;
        }

        TskSwitch( now, current );

    } while( 1 );

    for(;;);
}


void TskReschedule( struct SavedRegs_t regs ) {


#if 0
        DbgPrint( "\nCurrent Process:\n" );
        DbgPrint( "EAX = %8x, EBX = %8x, ECX = %8x, EDX = %8x\n"
                , regs.eax, regs.ebx, regs.ecx, regs.edx );
        DbgPrint( "ESI = %8x, EDI = %8x, EBP = %8x, EIP = %8x\n"
                , regs.esi, regs.edi, regs.ebp, regs.eip );
        DbgPrint( "DS = %8x, ES = %8x, CS = %8x\n"
                , regs.ds, regs.es, regs.cs );
        DbgPrint( "EFLAGS = %8x\n", regs.eflags );
#endif


#if 0
        tsks[ origtsk ].eax = regs.eax;
        tsks[ origtsk ].ebx = regs.ebx;
        tsks[ origtsk ].ecx = regs.ecx;
        tsks[ origtsk ].edx = regs.edx;
        tsks[ origtsk ].esi = regs.esi;
        tsks[ origtsk ].edi = regs.edi;
        tsks[ origtsk ].ebp = regs.ebp;
        tsks[ origtsk ].eip = regs.eip;
        tsks[ origtsk ].eflags = regs.eflags;

        regs.eax = tsks[ newtsk ].eax;
        regs.ebx = tsks[ newtsk ].ebx;
        regs.ecx = tsks[ newtsk ].ecx;
        regs.edx = tsks[ newtsk ].edx;
        regs.esi = tsks[ newtsk ].esi;
        regs.edi = tsks[ newtsk ].edi;
        regs.ebp = tsks[ newtsk ].ebp;
        regs.eip = tsks[ newtsk ].eip;
        regs.eflags = tsks[ newtsk ].eflags;
#endif
        

#if 0
        DbgPrint( "\nResume Process:\n" );
        DbgPrint( "EAX = %8x, EBX = %8x, ECX = %8x, EDX = %8x\n"
                , regs.eax, regs.ebx, regs.ecx, regs.edx );
        DbgPrint( "ESI = %8x, EDI = %8x, EBP = %8x, EIP = %8x\n"
                , regs.esi, regs.edi, regs.ebp, regs.eip );
        DbgPrint( "DS = %8x, ES = %8x, CS = %8x\n"
                , regs.ds, regs.es, regs.cs );
        DbgPrint( "EFLAGS = %8x\n", regs.eflags );
#endif


}


