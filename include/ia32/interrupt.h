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

#define     PIC1_REG0       0x20
#define     PIC1_REG1       0x21
#define     PIC2_REG0       0xa0
#define     PIC2_REG1       0xa1

#define     TRAP_START      0x00
#define     TRAP_END        0x07
#define     IRQ1_START      0x08
#define     IRQ1_END        0x0a
#define     IRQ2_START      0x70
#define     IRQ2_END        0x77


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
void ia32_IntDelIDT( __u8 index );

void ia32_IntDisable( void );
void ia32_IntEnable( void );

void ia32_IntInitPIC( void );
__u8 ia32_IntRegisterIRQ( __u8 num, __u32 handler );
__u8 ia32_IntUnregisterIRQ( __u8 num );
void ia32_IntEnableIRQ( __u8 num );
void ia32_IntDisableIRQ( __u8 num );


