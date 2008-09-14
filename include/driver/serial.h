/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: serial.h
 * Description:
 * 	None
 *
 */



//
// Defintions
//
#define UART0_BASE 			0x3F8
#define UART1_BASE 			0x2F8
#define UART_DEF_BASE		UART0_BASE


// UART registers
#define UART_RBR			0x00
#define UART_THR			0x00
#define UART_IER			0x01
#define UART_IIR			0x02
#define UART_FCR			0x02
#define UART_LCR			0x03
#define UART_MCR			0x04
#define UART_LSR			0x05
#define UART_MSR			0x06
#define UART_SPR			0x07
#define UART_ISR			0x08
#define UART_DLL			0x00
#define UART_DLH			0x01


// UART registers
#define UART_DEF_RBR		(UART_DEF_BASE + UART_RBR)
#define UART_DEF_THR		(UART_DEF_BASE + UART_THR)
#define UART_DEF_IER		(UART_DEF_BASE + UART_IER)
#define UART_DEF_IIR		(UART_DEF_BASE + UART_IIR)
#define UART_DEF_FCR		(UART_DEF_BASE + UART_FCR)
#define UART_DEF_LCR		(UART_DEF_BASE + UART_LCR)
#define UART_DEF_MCR		(UART_DEF_BASE + UART_MCR)
#define UART_DEF_LSR		(UART_DEF_BASE + UART_LSR)
#define UART_DEF_MSR		(UART_DEF_BASE + UART_MSR)
#define UART_DEF_SPR		(UART_DEF_BASE + UART_SPR)
#define UART_DEF_ISR		(UART_DEF_BASE + UART_ISR)
#define UART_DEF_DLL		(UART_DEF_BASE + UART_DLL)
#define UART_DEF_DLH		(UART_DEF_BASE + UART_DLH)


// Interrupt enable register (IER)
#define	UART_RAVIE			0x01
#define UART_TIE			0x02
#define	UART_RLSE			0x04
#define UART_MIE			0x08


// Line Control Register (LCR)
#define UART_DLAB			0x80
#define UART_SB				0x40
#define UART_STKYP			0x20
#define UART_EPS			0x10
#define UART_PEN			0x08

#define UART_STB_1			0x00
#define UART_STB_2			0x04

#define UART_WLS8			0x03
#define UART_WLS7			0x02
#define UART_WLS6			0x01
#define UART_WLS5			0x00


// FIFO Control Register (FCR)
#define UART_ITL14			0xC0
#define UART_ITL8			0x80
#define UART_ITL4			0x40
#define UART_ITL1			0x00

#define UART_DMA			0x08

#define UART_RESETTF		0x04
#define UART_RESETRF		0x02
#define UART_TRFIFOE		0x01


// Line Status Register (LSR)
#define UART_FIFOE			0x80
#define UART_TEMT			0x40
#define UART_TDRQ			0x20
#define UART_BI				0x10
#define UART_FE				0x08
#define UART_PE				0x04
#define UART_OE				0x02
#define UART_DR				0x01


// Modem Control Register (MCR)
#define UART_DTR			0x01
#define UART_RTS			0x02
#define UART_OUT1			0x04
#define UART_OUT2			0x08
#define UART_LOOPBACK		0x10


// Baud Rate
#define UART_BAUD_115200	0x01
#define UART_BAUD_38400		0x03
#define	UART_BAUD_19200		0x06
#define UART_BAUD_9600		0x0C
#define UART_BAUD_2400		0x30
#define UART_BAUD_1200		0x60



//
// Prototypes
//
void SrInit( void );
void SrInitInterrupt( void );
void SrIntHandler( u8 IrqNum );
void SrPutChar( s8 c );
s8 SrGetChar( void );



