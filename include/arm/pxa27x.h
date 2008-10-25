/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: pxa27x.h
 * Description:
 * 	Intel XScale PXA27x Processor
 *
 */



//
// See PXA25x Developer's Manual P3-37
// Coprocessor 14, Register 6
//
#define TURBO_MODE              0x00000001  // Enter Turbo Mode
#define FCS_SEQ                 0x00000002  // Enter Frequency Change Sequence


//
// See PXA25x Developer's Manual P3-38
// Coprocessor 14, Register 7
//
#define RUN_TURBO_MODE          0x00000000  // Run/Turbo Mode
#define IDLE_MODE               0x00000001  // Idle Mode
#define SLEEP_MODE              0x00000003  // Sleep Mode


//
// See ARM Architecture Reference Manual A2-9
// Program Status Registers(PSR)
// 
#define ARM_PSR_IRQ             0x00000080  // Disable Interrupt bit
#define ARM_PSR_FIQ             0x00000040  // Disable Fast Interrupt bit


#define ARM_PSR_USER_MODE       0x00000010  // CPU User Mode
#define ARM_PSR_FIQ_MODE        0x00000011  // CPU Fast Interrupt Mode
#define ARM_PSR_IRQ_MODE        0x00000012  // CPU Interrupt Mode
#define ARM_PSR_SUPERVISOR_MODE 0x00000013  // CPU Supervisor Mode
#define ARM_PSR_ABORT_MODE      0x00000017  // CPU Abort Mode
#define ARM_PSR_UNDEFINED_MODE  0x0000001b  // CPU Undefined Mode
#define ARM_PSR_SYSTEM_MODE     0x0000001f  // CPU System Mode



///////////////////////////////////////////////////////////////////////////////
// Internal Memory                                                           //
///////////////////////////////////////////////////////////////////////////////
#define PXA27X_INTMEM_BASE      0x5C000000
#define PXA27X_INTMEM_SIZE      0x40000

#define PXA27X_INTMEM_BANK0     0x5C000000
#define PXA27X_INTMEM_BANK1     0x5C010000
#define PXA27X_INTMEM_BANK2     0x5C020000
#define PXA27X_INTMEM_BANK3     0x5C030000
#define PXA27X_INTMEM_SZ_BANK   0x10000



///////////////////////////////////////////////////////////////////////////////
// SDRAM Memory                                                              //
///////////////////////////////////////////////////////////////////////////////
#define PXA27X_SDRAM_BASE       0xA0000000
#define PXA27X_SDRAM_SIZE       0x10000000



///////////////////////////////////////////////////////////////////////////////
// UART Hardware Unit                                                        //
///////////////////////////////////////////////////////////////////////////////
#define PXA27X_UART_RBR         0x00
#define PXA27X_UART_THR         0x00
#define PXA27X_UART_IER         0x04
#define PXA27X_UART_IIR         0x08
#define PXA27X_UART_FCR         0x08
#define PXA27X_UART_LCR         0x0C
#define PXA27X_UART_MCR         0x10
#define PXA27X_UART_LSR         0x14
#define PXA27X_UART_MSR         0x18
#define PXA27X_UART_SPR         0x1C
#define PXA27X_UART_ISR         0x20
#define PXA27X_UART_DLL         0x00
#define PXA27X_UART_DLH         0x04


// Full Function UART
#define PXA27X_FFUART_BASE      0x40100000
#define PXA27X_FFUART_RBR       (PXA27X_FFUART_BASE + PXA27X_UART_RBR)
#define PXA27X_FFUART_THR       (PXA27X_FFUART_BASE + PXA27X_UART_THR)
#define PXA27X_FFUART_IER       (PXA27X_FFUART_BASE + PXA27X_UART_IER)
#define PXA27X_FFUART_IIR       (PXA27X_FFUART_BASE + PXA27X_UART_IIR)
#define PXA27X_FFUART_FCR       (PXA27X_FFUART_BASE + PXA27X_UART_FCR)
#define PXA27X_FFUART_LCR       (PXA27X_FFUART_BASE + PXA27X_UART_LCR)
#define PXA27X_FFUART_MCR       (PXA27X_FFUART_BASE + PXA27X_UART_MCR)
#define PXA27X_FFUART_LSR       (PXA27X_FFUART_BASE + PXA27X_UART_LSR)
#define PXA27X_FFUART_MSR       (PXA27X_FFUART_BASE + PXA27X_UART_MSR)
#define PXA27X_FFUART_SPR       (PXA27X_FFUART_BASE + PXA27X_UART_SPR)
#define PXA27X_FFUART_ISR       (PXA27X_FFUART_BASE + PXA27X_UART_ISR)
#define PXA27X_FFUART_DLL       (PXA27X_FFUART_BASE + PXA27X_UART_DLL)
#define PXA27X_FFUART_DLH       (PXA27X_FFUART_BASE + PXA27X_UART_DLH)


// Bluetooth UART
#define PXA27X_BTUART_BASE      0x40200000
#define PXA27X_BTUART_RBR       (PXA27X_BTUART_BASE + PXA27X_UART_RBR)
#define PXA27X_BTUART_THR       (PXA27X_BTUART_BASE + PXA27X_UART_THR)
#define PXA27X_BTUART_IER       (PXA27X_BTUART_BASE + PXA27X_UART_IER)
#define PXA27X_BTUART_IIR       (PXA27X_BTUART_BASE + PXA27X_UART_IIR)
#define PXA27X_BTUART_FCR       (PXA27X_BTUART_BASE + PXA27X_UART_FCR)
#define PXA27X_BTUART_LCR       (PXA27X_BTUART_BASE + PXA27X_UART_LCR)
#define PXA27X_BTUART_MCR       (PXA27X_BTUART_BASE + PXA27X_UART_MCR)
#define PXA27X_BTUART_LSR       (PXA27X_BTUART_BASE + PXA27X_UART_LSR)
#define PXA27X_BTUART_MSR       (PXA27X_BTUART_BASE + PXA27X_UART_MSR)
#define PXA27X_BTUART_SPR       (PXA27X_BTUART_BASE + PXA27X_UART_SPR)
#define PXA27X_BTUART_ISR       (PXA27X_BTUART_BASE + PXA27X_UART_ISR)
#define PXA27X_BTUART_DLL       (PXA27X_BTUART_BASE + PXA27X_UART_DLL)
#define PXA27X_BTUART_DLH       (PXA27X_BTUART_BASE + PXA27X_UART_DLH)


// Standard UART
#define PXA27X_STUART_BASE      0x40700000
#define PXA27X_STUART_RBR       (PXA27X_STUART_BASE + PXA27X_UART_RBR)
#define PXA27X_STUART_THR       (PXA27X_STUART_BASE + PXA27X_UART_THR)
#define PXA27X_STUART_IER       (PXA27X_STUART_BASE + PXA27X_UART_IER)
#define PXA27X_STUART_IIR       (PXA27X_STUART_BASE + PXA27X_UART_IIR)
#define PXA27X_STUART_FCR       (PXA27X_STUART_BASE + PXA27X_UART_FCR)
#define PXA27X_STUART_LCR       (PXA27X_STUART_BASE + PXA27X_UART_LCR)
#define PXA27X_STUART_MCR       (PXA27X_STUART_BASE + PXA27X_UART_MCR)
#define PXA27X_STUART_LSR       (PXA27X_STUART_BASE + PXA27X_UART_LSR)
#define PXA27X_STUART_MSR       (PXA27X_STUART_BASE + PXA27X_UART_MSR)
#define PXA27X_STUART_SPR       (PXA27X_STUART_BASE + PXA27X_UART_SPR)
#define PXA27X_STUART_ISR       (PXA27X_STUART_BASE + PXA27X_UART_ISR)
#define PXA27X_STUART_DLL       (PXA27X_STUART_BASE + PXA27X_UART_DLL)
#define PXA27X_STUART_DLH       (PXA27X_STUART_BASE + PXA27X_UART_DLH)


// Interrupt Enable Register (IER)
#define PXA27X_UART_DMAE        0x80
#define PXA27X_UART_UUE         0x40
#define PXA27X_UART_NRZE        0x20
#define PXA27X_UART_RTOIE       0x10
#define PXA27X_UART_MIE         0x08
#define PXA27X_UART_RLSE        0x04
#define PXA27X_UART_TIE         0x02
#define PXA27X_UART_RAVIE       0x01


// Line Control Register (LCR)
#define PXA27X_UART_DLAB        0x80
#define PXA27X_UART_SB          0x40
#define PXA27X_UART_STKYP       0x20
#define PXA27X_UART_EPS         0x10
#define PXA27X_UART_PEN         0x08
#define PXA27X_UART_STB         0x04
#define PXA27X_UART_WLS8        0x03
#define PXA27X_UART_WLS7        0x02
#define PXA27X_UART_WLS6        0x01
#define PXA27X_UART_WLS5        0x00


// FIFO Control Register (FCR)
#define PXA27X_UART_ITL32       0x03
#define PXA27X_UART_ITL16       0x02
#define PXA27X_UART_ITL8        0x01
#define PXA27X_UART_ITL1        0x00
#define PXA27X_UART_RESETTF     0x04
#define PXA27X_UART_RESETRF     0x02
#define PXA27X_UART_TRFIFOE     0x01


// Line Status Register (LSR)
#define PXA27X_UART_FIFOE       0x80
#define PXA27X_UART_TEMT        0x40
#define PXA27X_UART_TDRQ        0x20
#define PXA27X_UART_BI          0x10
#define PXA27X_UART_FE          0x08
#define PXA27X_UART_PE          0x04
#define PXA27X_UART_OE          0x02
#define PXA27X_UART_DR          0x01


// UART Baud Rate
#define PXA27X_BAUD_115200      0x08
#define PXA27X_BAUD_38400       0x18


// Default Console UART
#define PXA27X_UART_DEFBASE     PXA27X_FFUART_BASE



