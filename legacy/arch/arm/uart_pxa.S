/*
 * Copyright (C) 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * uart_pxa.S -- OluxOS UART code for
 *               Intel XScale PXA27x Processor
 *
 */


#include "pxa27x.h"


.file       "uart_pxa.S"


///////////////////////////////////////////////////////////////////////////////
// Code Segment                                                              //
///////////////////////////////////////////////////////////////////////////////
.text
.global     UartInitialize, UartTransmit, UartTransmitBuffer
.align      4
.arm



///////////////////////////////////////////////////////////////////////////////
// UartInitialize -- Initialize UART Hardware Unit
//
// Input:
//  None
//
// Output:
//  None
//
// Modified:
//  R0
//  R1
//
UartInitialize:


    // Save base address
    ldr     r0, =PXA27X_UART_DEFBASE


    // Disable Std UART interrupts
    add     r0, r0, #0x04                       // Base + 0x04, PXA27X_UART_IER
    eor     r1, r1, r1                          // R1 = 0
    orr     r1, r1, #PXA27X_UART_UUE            // Enable Std UART unit
    str     r1, [r0]                            // Write register
    

    // DLAB = 1
    sub     r0, r0, #0x08                       // Base + 0x0C, PXA27X_UART_LCR
    ldr     r1, [r0]
    orr     r1, r1, #PXA27X_UART_DLAB           //
    str     r1, [r0]                            // DLAB = 1


    // Set Baudrate = 115200 bps
    sub     r0, r0, #0x0C                       // Base + 0x00, PXA27X_FFUART_DLL
    ldr     r1, =PXA27X_BAUD_115200
    str     r1, [r0], #0x04                     // Low byte of Divisor, Post Index
    eor     r1, r1, r1                          // R1 = 0
    str     r1, [r0]                            // High byte of Divisor, PXA27X_FFUART_DLH, Base + 0x04


    // DLAB = 0, set 8-N-1
    ldr     r0, =PXA27X_FFUART_LCR
    add     r0, r0, #0x08                       // Base + 0x0C, PXA27X_UART_LCR
    ldr     r1, [r0]
    bic     r1, r1, #PXA27X_UART_DLAB            // DLAB = 0
    orr     r1, r1, #PXA27X_UART_WLS8           // 8-bit, no parity, 1 stop bit
    str     r1, [r0]


    // Enable FIFO, 1 byte
    sub     r0, r0, #0x04                       // Base + 0x08, PXA27X_UART_FCR
    ldr     r1, =( PXA27X_UART_TRFIFOE | PXA27X_UART_ITL1 )
    str     r1, [r0]


    // Return
    bx      lr



///////////////////////////////////////////////////////////////////////////////
// UartTransmit -- Transmit one byte via UART
//
// Input:
//  None
//
// Output:
//  None
//
// Modified:
//  R0
//  R1
//
UartTransmit:


    // Send a byte
    ldr     r0, =PXA27X_UART_DEFBASE    // Load base address
    strb    r1, [r0], #PXA27X_UART_LSR  // Send a byte and offset to 0x14


_WaitTrans:


    // Read flags
    ldr     r1, [r0]


    // Check for Transmit Data Request
    tst     r1, #PXA27X_UART_TDRQ
    beq     _WaitTrans


    // Return
    bx      lr



///////////////////////////////////////////////////////////////////////////////
// UartTransmitBuffer -- Transmit buffer via UART
//
// Input:
//  R2      -- Pointer of buffer
//
// Output:
//  None
//
// Modified:
//  R0
//  R1
//  R2
//  R3
//
UartTransmitBuffer:


    // Save LR
    mov     r3, lr


_UartTransmitBufferNextByte:


    // Read one byte and offset to next
    ldrb    r1, [r2], #0x01
    and     r1, r1, #0xFF


    // NULL character
    cmp     r1, #0x00
    beq     _UartTransmitBufferExit


    // Sent one character
    bl      UartTransmit
    b       _UartTransmitBufferNextByte


_UartTransmitBufferExit:


    // Return
    mov     lr, r3
    bx      lr




///////////////////////////////////////////////////////////////////////////////
// Data Section                                                              //
///////////////////////////////////////////////////////////////////////////////




