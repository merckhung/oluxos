/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     PIC_MASTER_CMD      0x20
#define     PIC_MASTER_IMR      0x21
#define     PIC_MASTER_ICW1     PIC_MASTER_CMD
#define     PIC_MASTER_ICW2     PIC_MASTER_IMR
#define     PIC_MASTER_ICW3     PIC_MASTER_IMR
#define     PIC_MASTER_ICW4     PIC_MASTER_IMR

#define     PIC_MASTER_OCW1     PIC_MASTER_IMR
#define     PIC_MASTER_OCW2     PIC_MASTER_CMD
#define     PIC_MASTER_OCW3     PIC_MASTER_CMD


#define     PIC_SLAVE_CMD       0xA0
#define     PIC_SLAVE_IMR       0xA1
#define     PIC_SLAVE_ICW1      PIC_SLAVE_CMD
#define     PIC_SLAVE_ICW2      PIC_SLAVE_IMR
#define     PIC_SLAVE_ICW3      PIC_SLAVE_IMR
#define     PIC_SLAVE_ICW4      PIC_SLAVE_IMR

#define     PIC_SLAVE_OCW1      PIC_SLAVE_IMR
#define     PIC_SLAVE_OCW2      PIC_SLAVE_CMD
#define     PIC_SLAVE_OCW3      PIC_SLAVE_CMD


#define     PIC_CASCADE_IR      2
#define     PIC_IRQ_BASE		0x20
#define		PIC_IRQ_SLAVE		PIC_IRQ_BASE + 8


void i8259Init( void );
void i8259EnableIRQ( __u8 irqnum );
void i8259DisableIRQ( __u8 irqnum );
void i8259IssueEOI( void );


