/*
 * Copyright (C) 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * msm8x50.h -- Qualcomm MSM 8x50 Processor
 *
 */




///////////////////////////////////////////////////////////////////////////////
// UART Hardware Unit                                                        //
///////////////////////////////////////////////////////////////////////////////
#define MSM8x50_UART_RBR            0x00
#define MSM8x50_UART_THR            0x00
#define MSM8x50_UART_IER            0x04
#define MSM8x50_UART_IIR            0x08
#define MSM8x50_UART_FCR            0x08
#define MSM8x50_UART_LCR            0x0C
#define MSM8x50_UART_MCR            0x10
#define MSM8x50_UART_LSR            0x14
#define MSM8x50_UART_MSR            0x18
#define MSM8x50_UART_SPR            0x1C
#define MSM8x50_UART_ISR            0x20
#define MSM8x50_UART_DLL            0x00
#define MSM8x50_UART_DLH            0x04


// Full Function UART
#define MSM8x50_FFUART_BASE         0x40100000
#define MSM8x50_FFUART_RBR          (MSM8x50_FFUART_BASE + MSM8x50_UART_RBR)
#define MSM8x50_FFUART_THR          (MSM8x50_FFUART_BASE + MSM8x50_UART_THR)
#define MSM8x50_FFUART_IER          (MSM8x50_FFUART_BASE + MSM8x50_UART_IER)
#define MSM8x50_FFUART_IIR          (MSM8x50_FFUART_BASE + MSM8x50_UART_IIR)
#define MSM8x50_FFUART_FCR          (MSM8x50_FFUART_BASE + MSM8x50_UART_FCR)
#define MSM8x50_FFUART_LCR          (MSM8x50_FFUART_BASE + MSM8x50_UART_LCR)
#define MSM8x50_FFUART_MCR          (MSM8x50_FFUART_BASE + MSM8x50_UART_MCR)
#define MSM8x50_FFUART_LSR          (MSM8x50_FFUART_BASE + MSM8x50_UART_LSR)
#define MSM8x50_FFUART_MSR          (MSM8x50_FFUART_BASE + MSM8x50_UART_MSR)
#define MSM8x50_FFUART_SPR          (MSM8x50_FFUART_BASE + MSM8x50_UART_SPR)
#define MSM8x50_FFUART_ISR          (MSM8x50_FFUART_BASE + MSM8x50_UART_ISR)
#define MSM8x50_FFUART_DLL          (MSM8x50_FFUART_BASE + MSM8x50_UART_DLL)
#define MSM8x50_FFUART_DLH          (MSM8x50_FFUART_BASE + MSM8x50_UART_DLH)


// Interrupt Enable Register (IER)
#define MSM8x50_UART_DMAE           0x80
#define MSM8x50_UART_UUE            0x40
#define MSM8x50_UART_NRZE           0x20
#define MSM8x50_UART_RTOIE          0x10
#define MSM8x50_UART_MIE            0x08
#define MSM8x50_UART_RLSE           0x04
#define MSM8x50_UART_TIE            0x02
#define MSM8x50_UART_RAVIE          0x01


// Line Control Register (LCR)
#define MSM8x50_UART_DLAB           0x80
#define MSM8x50_UART_SB             0x40
#define MSM8x50_UART_STKYP          0x20
#define MSM8x50_UART_EPS            0x10
#define MSM8x50_UART_PEN            0x08
#define MSM8x50_UART_STB            0x04
#define MSM8x50_UART_WLS8           0x03
#define MSM8x50_UART_WLS7           0x02
#define MSM8x50_UART_WLS6           0x01
#define MSM8x50_UART_WLS5           0x00


// FIFO Control Register (FCR)
#define MSM8x50_UART_ITL32          0x03
#define MSM8x50_UART_ITL16          0x02
#define MSM8x50_UART_ITL8           0x01
#define MSM8x50_UART_ITL1           0x00
#define MSM8x50_UART_RESETTF        0x04
#define MSM8x50_UART_RESETRF        0x02
#define MSM8x50_UART_TRFIFOE        0x01


// Line Status Register (LSR)
#define MSM8x50_UART_FIFOE          0x80
#define MSM8x50_UART_TEMT           0x40
#define MSM8x50_UART_TDRQ           0x20
#define MSM8x50_UART_BI             0x10
#define MSM8x50_UART_FE             0x08
#define MSM8x50_UART_PE             0x04
#define MSM8x50_UART_OE             0x02
#define MSM8x50_UART_DR             0x01


// UART Baud Rate
#define MSM8x50_BAUD_115200         0x08
#define MSM8x50_BAUD_38400          0x18


