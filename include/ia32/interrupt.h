/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     INTTBL_SIZE     256
#define     INTENTRY_SIZE   8


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


void ia32_IntSetupIDT( void );
void ia32_IntSetIDT( __u8 index, __u32 offset, __u16 seg, __u8 flag );

void ia32_IntDisable( void );
void ia32_IntEnable( void );


