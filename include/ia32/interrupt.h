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

#define     SZ_IRQ_STACK    4096


#define     IRQHandler(irqnum)      (u32)PreliminaryInterruptHandler_##irqnum


#ifndef __ASM__
extern void InterruptHandler( void );


struct IDTEntry_t {

    u16   OffsetLSW;
    u16   SegSelect;
    u8    Reserved;
    u8    Flag;
    u16   OffsetMSW;

} __attribute__ ((packed));


struct IDTPtr_t {

    u16   Limit;
    u32   BaseAddr;

} __attribute__ ((packed));


struct IntHandlerLst_t {

    void (*Handler)( u8 irqnum );
};


struct SavedRegs_t {

    // Saved by Handler
    u32   edi;
    u32   esi;
    u32   ebp;
    u32   ebx;
    u32   edx;
    u32   ecx;
    u32   eax;
    u32   gs;
    u32   fs;
    u32   ds;
    u32   ss;
    u32   es;

    // Saved by Processor
    u32   eip;
    u32   cs;
    u32   eflags;

} __attribute__ ((packed));


struct IRQStack_t {

    u32   esp;
    s8    stack[ SZ_IRQ_STACK ];
};


void IntInitInterrupt( void );

void IntSetIDT( u8 index, u32 offset, u16 seg, u8 flag );
void IntDelIDT( u8 index );

void IntDisable( void );
void IntEnable( void );

void IntRegIRQ( u8 irqnum, u32 handler, void (*IRQHandler)( u8 ) );
void IntUnregIRQ( u8 irqnum );

void IntIssueEOI( void );
#endif


