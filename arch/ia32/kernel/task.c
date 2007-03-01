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
#include <clib.h>


#define NR_TASK 3


static TSS_t TskTSSEntry[ NR_TASK ];
static TSSD_t TskTSSDEntry[ NR_TASK ];
static Task_t tsks[ 2 ];
static char stack_buf[ 2 ][ 1024 ];

extern void __task_seg( void );


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


void TaskInitTSSD( void ) {

    __s32 i;

    memset( &TskTSSDEntry, 0, sizeof( TSSD_t ) * NR_TASK );
    for( i = 0 ; i < NR_TASK ; i++ ) {
    
        TskTSSDEntry[ i ].limit0 = 0xffff;
        TskTSSDEntry[ i ].limit1 = 0x0f;
        TskTSSDEntry[ i ].baseaddr0 = (0x0000ffff | (__u32)TskTest1);
        TskTSSDEntry[ i ].baseaddr1 = (0x00ff0000 | (__u32)TskTest1) >> 16;
        TskTSSDEntry[ i ].baseaddr2 = (0xff000000 | (__u32)TskTest1) >> 24;
        TskTSSDEntry[ i ].flag = 0x09;
    }

    __asm__ __volatile__ (
        "ltr    %%ax\n"
        :
        : "g" (TskTSSDEntry)
    );

}


void TskInitTSS( void ) {

    memset( &TskTSSEntry, 0, sizeof( TSS_t ) * NR_TASK );
}


void TskInit( void ) {

    tsks[ 0 ].eip       = (__u32)TskTest1;
    tsks[ 0 ].cs        = (__u16)TskTest1;
    tsks[ 0 ].ss        = 0x18;
    tsks[ 0 ].esp       = (__u32)stack_buf[ 0 ] + 1024;
    tsks[ 0 ].eflags    = 0x200;

    tsks[ 1 ].eip       = (__u32)TskTest2;
    tsks[ 1 ].cs        = (__u16)TskTest2;
    tsks[ 1 ].ss        = 0x18;
    tsks[ 1 ].esp       = (__u32)stack_buf[ 1 ] + 1024;
    tsks[ 1 ].eflags    = 0x200;
}


void TakSwitch( void ) {

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
        "jmp    %9\n"
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

    TakSwitch();
    for(;;);
}



