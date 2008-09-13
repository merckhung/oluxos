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


static TSS_t KrnTSS, UsrTSS;

static Task_t tsks[ 2 ];
static char stack_buf[ 2 ][ 1024 ];

extern void __ktask_seg( void );
extern void __utask_seg( void );
extern void KrnTSSD( void );
extern void UsrTSSD( void );

static u32 current = -1;

extern void __KERNEL_CS( void );
extern void __KERNEL_DS( void );
extern void __USER_CS( void );
extern void __USER_DS( void );
extern void __ldt_seg( void );

//extern volatile u32  *usermem;

void TskTest1( void ) {

    u32 i, j;

    for( i = 0, j = 0 ; ; i++ ) {
    
        if( !(i % 90000000) ) {

            DbgPrint( "Task1: %d\n", j );
            j++;
        }
    }
}


void TskTest2( void ) {

    u32 i, j;

    for( i = 0, j = 0 ; ; i++ ) {
    
        if( !(i % 50000000) ) {

            DbgPrint( "Task2: %d\n", j );
            j++;
        }
    }
}


void TskUsr1( void ) {

    __asm__ __volatile__ (

        "test1:\n"
        "incl   %eax\n"
        "jmp    test1\n"
    );
}


void TskUsr2( void ) {

    __asm__ __volatile__ (

        "test2:\n"
        "incl   %ebx\n"
        "jmp    test2\n"
    );
}


void TskInit( void ) {

    TSSD_t *p = (TSSD_t *)KrnTSSD;
    TSSD_t *u = (TSSD_t *)UsrTSSD;


    //
    // Override TSSD default value
    //
    p->limit0       = (0x0000ffff & sizeof( TSS_t ));
    p->limit1       = (0x000f0000 & sizeof( TSS_t )) >> 16;
    p->baseaddr0    = (0x0000ffff & (u32)&KrnTSS);
    p->baseaddr1    = (0x00ff0000 & (u32)&KrnTSS) >> 16;
    p->baseaddr2    = (0xff000000 & (u32)&KrnTSS) >> 24;
    p->flag         = 0x89;


    u->limit0       = (0x0000ffff & sizeof( TSS_t ));
    u->limit1       = (0x000f0000 & sizeof( TSS_t )) >> 16;
    u->baseaddr0    = (0x0000ffff & (u32)&UsrTSS);
    u->baseaddr1    = (0x00ff0000 & (u32)&UsrTSS) >> 16;
    u->baseaddr2    = (0xff000000 & (u32)&UsrTSS) >> 24;
    u->flag         = 0xe9;


    //DbgPrint( "usermem = 0x%8X, value = 0x%8X\n", usermem, *usermem );
    //CbMemCpy( (void *)usermem, TskUsr1, 512 );


    //
    // Load Kernel TSS Descriptor
    //
    __asm__ __volatile__ (

        "movw   %0, %%ax\n"
        "ltr    %%ax\n"
        :: "g" (__ktask_seg)
        : "ax"
    );


    //
    // Load LDTR
    //
    __asm__ __volatile__ (
    
        "movw   %0, %%ax\n"
        "lldt   %%ax\n"
        :: "g" (__ldt_seg)
        : "ax"
    );


    //
    // Fill up Task descriptors
    //
    tsks[ 0 ].eip       = (u32)TskUsr1;
    tsks[ 0 ].cs        = (u32)__USER_CS | 0x03;
    tsks[ 0 ].ss        = (u32)__USER_DS | 0x03;
    tsks[ 0 ].ds        = (u32)__USER_DS | 0x03;
    tsks[ 0 ].es        = (u32)__USER_DS | 0x03;
    tsks[ 0 ].fs        = (u32)__USER_DS | 0x03;
    tsks[ 0 ].gs        = (u32)__USER_DS | 0x03;
    tsks[ 0 ].esp       = (u32)stack_buf[ 0 ] + 1024;
    tsks[ 0 ].eflags    = 0x3000;


    UsrTSS.eip          = tsks[ 0 ].eip;
    UsrTSS.cs           = tsks[ 0 ].cs;
    UsrTSS.ss           = tsks[ 0 ].ss;
    UsrTSS.ds           = tsks[ 0 ].ds;
    UsrTSS.es           = tsks[ 0 ].es;
    UsrTSS.fs           = tsks[ 0 ].fs;
    UsrTSS.gs           = tsks[ 0 ].gs;
    UsrTSS.esp          = tsks[ 0 ].esp;
    UsrTSS.eflags       = tsks[ 0 ].eflags;


    KrnTSS.cs           = (u16)__KERNEL_CS;
    KrnTSS.ss           = (u16)__KERNEL_DS;
    KrnTSS.ds           = (u16)__KERNEL_DS;
    KrnTSS.es           = (u16)__KERNEL_DS;
    KrnTSS.fs           = (u16)__KERNEL_DS;
    KrnTSS.gs           = (u16)__KERNEL_DS;


    tsks[ 1 ].eip       = (u32)TskUsr2;
    tsks[ 1 ].cs        = (u32)__USER_CS | 0x03;
    tsks[ 1 ].ss        = (u32)__USER_DS | 0x03;
    tsks[ 1 ].ds        = (u32)__USER_DS | 0x03;
    tsks[ 1 ].es        = (u32)__USER_DS | 0x03;
    tsks[ 1 ].fs        = (u32)__USER_DS | 0x03;
    tsks[ 1 ].gs        = (u32)__USER_DS | 0x03;
    tsks[ 1 ].esp       = (u32)stack_buf[ 1 ] + 1024;
    tsks[ 1 ].eflags    = 0x3000;
}


void TskStart( void ) {

    u16   uts = ((u16)__utask_seg) | 0x03;


    //
    // Load User TSS Descriptor
    //
    __asm__ __volatile__ (

        "movw   %0, %%ax\n"
        "ltr    %%ax\n"
        :: "g" (uts)
        : "ax"
    );


    //
    // Jump to start first user space task
    //
    __asm__ __volatile__ (

        "xorl   %%eax, %%eax\n"
        "movw   %0, %%ax\n"
        "pushl  %%eax\n"            // SS
        "movl   %1, %%eax\n"
        "pushl  %%eax\n"            // ESP
        "movl   %2, %%eax\n"
        "pushl  %%eax\n"            // EFLAGS
        "xorl   %%eax, %%eax\n"
        "movw   %3, %%ax\n"
        "pushl  %%eax\n"            // CS
        "movl   %4, %%eax\n"
        "pushl  %%eax\n"            // EIP
        "movl   %5, %%ecx\n"        // ECX
        "movl   %6, %%edx\n"        // EDX
        "movl   %7, %%ebx\n"        // EBX
        "movl   %8, %%ebp\n"        // EBP
        "movl   %9, %%esi\n"        // ESI
        "movl   %10, %%edi\n"       // EDI
        "movw   %11, %%ax\n"
        "movw   %%ax, %%fs\n"       // FS
        "movw   %12, %%ax\n"
        "movw   %%ax, %%gs\n"       // GS
        "movw   %13, %%ax\n"
        "movw   %%ax, %%es\n"       // ES
        "movl   %14, %%eax\n"
        "pushl  %%eax\n"            // Prepare EAX
        "movw   %15, %%ax\n"
        "movw   %%ax, %%ds\n"       // DS
        "popl   %%eax\n"            // EAX
        "jmp    .\n"
        "iret\n"                    // Switch to new task
        :: "g" (tsks[ 0 ].ss),
           "g" (tsks[ 0 ].esp),
           "g" (tsks[ 0 ].eflags),
           "g" (tsks[ 0 ].cs),
           "g" (tsks[ 0 ].eip),
           "g" (tsks[ 0 ].ecx),
           "g" (tsks[ 0 ].edx),
           "g" (tsks[ 0 ].ebx),
           "g" (tsks[ 0 ].ebp),
           "g" (tsks[ 0 ].esi),
           "g" (tsks[ 0 ].edi),
           "g" (tsks[ 0 ].fs),
           "g" (tsks[ 0 ].gs),
           "g" (tsks[ 0 ].es),
           "g" (tsks[ 0 ].eax),
           "g" (tsks[ 0 ].ds)
    );
}


void TskSwitch( u32 origtsk, u32 newtsk ) {


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

}


void TskScheduler( void ) {


    u32 now;

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


