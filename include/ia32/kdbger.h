#ifndef __PACKET_H__
#define __PACKET_H__
/*
 * Copyright (C) 2006 - 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: kdbger.h
 * Description:
 *  OluxOS Kernel Debugger header file
 *
 */

// 16550A UART registers
#define UART_REG_RBR 0x00  // RO
#define UART_REG_THR 0x00  // WO
#define UART_REG_IER 0x01  // RW
#define UART_REG_IIR 0x02  // RO
#define UART_REG_FCR 0x02  // WO
#define UART_REG_LCR 0x03  // RW
#define UART_REG_MCR 0x04  // RW
#define UART_REG_LSR 0x05  // RW
#define UART_REG_MSR 0x06  // RW
#define UART_REG_SCR 0x07  // RW
#define UART_REG_DLL 0x00  // RW
#define UART_REG_DLH 0x01  // RW

// IER
#define UART_IER_RXRD 0x01
#define UART_IER_TBE 0x02
#define UART_IER_ERBK 0x04
#define UART_IER_SINP 0x08

// IIR
#define UART_IIR_PND 0x01
#define UART_IIR_ID_MASK 0x06
#define UART_IIR_ID_OFFET 1
#define UART_IIR_CHG_INPUT 0x00
#define UART_IIR_TBE 0x01
#define UART_IIR_DR 0x02
#define UART_IIR_SEB 0x03

// FCR
#define UART_FCR_FE 0x01
#define UART_FCR_RFR 0x02
#define UART_FCR_XFR 0x04
#define UART_FCR_DMS 0x08
#define UART_FCR_TL_1B 0x00
#define UART_FCR_TL_4B 0x40
#define UART_FCR_TL_8B 0x80
#define UART_FCR_TL_14B 0xC0

// LCR
#define UART_LCR_DLAB 0x80
#define UART_LCR_BRK 0x40

#define UART_PARITY_NONE 0x00
#define UART_PARITY_ODD 0x08
#define UART_PARITY_EVEN 0x18
#define UART_PARITY_MARK 0x28
#define UART_PARITY_SPACE 0x38

#define UART_STOPBIT_1 0x00
#define UART_STOPBIT_2 0x04

#define UART_DATABIT_8 0x03
#define UART_DATABIT_7 0x02
#define UART_DATABIT_6 0x01
#define UART_DATABIT_5 0x00

// MCR
#define UART_DTR 0x01
#define UART_RTS 0x02
#define UART_OUT1 0x04
#define UART_OUT2 0x08
#define UART_LOOPBACK 0x10

// LSR
#define UART_LSR_RXDR 0x01
#define UART_LSR_OVER 0x02
#define UART_LSR_PARE 0x04
#define UART_LSR_FRME 0x08
#define UART_LSR_BREK 0x10
#define UART_LSR_TBE 0x20
#define UART_LSR_TXE 0x40
#define UART_LSR_FIFOE 0x80

// MSR
#define UART_MSR_DCTS 0x01
#define UART_MSR_DDSR 0x02
#define UART_MSR_TERI 0x04
#define UART_MSR_DDCD 0x08
#define UART_MSR_CTS 0x10
#define UART_MSR_DSR 0x20
#define UART_MSR_RI 0x40
#define UART_MSR_DCD 0x80

// Baud Rate
#define UART_BAUD_115200 0x01
#define UART_BAUD_38400 0x03
#define UART_BAUD_19200 0x06
#define UART_BAUD_9600 0x0C
#define UART_BAUD_2400 0x30
#define UART_BAUD_1200 0x60

#include <kdbger_pkt.h>

typedef enum {

  UART_PORT0 = 0x03F8,
  UART_PORT1 = 0x02F8,

} kdbgerDebugPort_t;

typedef enum {

  KDBGER_BAUD_115200 = 0x01,
  KDBGER_BAUD_38400 = 0x03,
  KDBGER_BAUD_19200 = 0x06,
  KDBGER_BAUD_9600 = 0x0C,
  KDBGER_BAUD_2400 = 0x30,
  KDBGER_BAUD_1200 = 0x60,

} kdbgerBaudrate_t;

void kdbgerInitialization(kdbgerDebugPort_t port);
#endif
