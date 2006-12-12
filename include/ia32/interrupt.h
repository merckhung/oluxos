/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     INTTBL_SIZE     256
#define     INTENTRY_SIZE   8

#define     TRAP_START      0x00
#define     TRAP_END        0x1f

#define     INT_GATE_FLAG   0x8e
#define     TRAP_GATE_FLAG  0x8f


extern void ia32_InterruptHandler( void );


struct ia32_IDTEntry_t {

    __u16   OffsetLSW;
    __u16   SegSelect;
    __u8    Reserved;
    __u8    Flag;
    __u16   OffsetMSW;

} __attribute__ ((packed));


struct ia32_IDTPtr_t {

    __u16   Limit;
    __u32   BaseAddr;

} __attribute__ ((packed));


void ia32_IntInitInterrupt( void );

void ia32_IntSetIDT( __u8 index, __u32 offset, __u16 seg, __u8 flag );
void ia32_IntDelIDT( __u8 index );

void ia32_IntDisable( void );
void ia32_IntEnable( void );

void ia32_IntRegIRQ( __u8 irqnum, __u32 handler );
void ia32_IntUnregIRQ( __u8 irqnum );


