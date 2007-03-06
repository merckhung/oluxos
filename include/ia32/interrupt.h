/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     NR_INTERRUPT    256
#define     SZ_INTENTRY     8

#define     TRAP_START      0x00
#define     TRAP_END        0x1f

#define     INT_GATE_FLAG   0x8e
#define     TRAP_GATE_FLAG  0x8f


#define     IRQHandler(irqnum)      (__u32)PreliminaryInterruptHandler_##irqnum


#ifndef __ASM__
extern void InterruptHandler( void );


struct IDTEntry_t {

    __u16   OffsetLSW;
    __u16   SegSelect;
    __u8    Reserved;
    __u8    Flag;
    __u16   OffsetMSW;

} __attribute__ ((packed));


struct IDTPtr_t {

    __u16   Limit;
    __u32   BaseAddr;

} __attribute__ ((packed));


struct IntHandlerLst_t {

    void (*Handler)( __u8 irqnum );
};


struct SavedRegs_t {

    // Saved by Handler
    __u32   edi;
    __u32   esi;
    __u32   ebp;
    __u32   ebx;
    __u32   edx;
    __u32   ecx;
    __u32   eax;
    __u32   gs;
    __u32   fs;
    __u32   ds;
    __u32   ss;
    __u32   es;

    // Saved by Processor
    __u32   eip;
    __u32   cs;
    __u32   eflags;

} __attribute__ ((packed));


void IntInitInterrupt( void );

void IntSetIDT( __u8 index, __u32 offset, __u16 seg, __u8 flag );
void IntDelIDT( __u8 index );

void IntDisable( void );
void IntEnable( void );

void IntRegIRQ( __u8 irqnum, __u32 handler, void (*IRQHandler)( __u8 ) );
void IntUnregIRQ( __u8 irqnum );

void IntIssueEOI( void );
#endif


