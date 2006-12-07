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

    __u16   OffsetLSW : 16;
    __u16   SegSelect : 16;
    __u8    Reserved  : 8;
    __u8    Flag      : 8;
    __u16   OffsetMSW : 16;

} __attribute__ ((packed));


struct ia32_IDTPtr_t {

    __u16   Limit    : 16;
    __u32   BaseAddr : 32;

} __attribute__ ((packed));


void ia32_IntInitHandler( void );
void ia32_IntSetupIDT( void );
void ia32_IntHandler( void );


