/*
 * Copyright (C) 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * uart_msm.S -- OluxOS UART code for
 *               Qualcomm MSM 8x50 Processor
 *
 */


#include "msm8x50.h"


.file       "uart_msm.S"


///////////////////////////////////////////////////////////////////////////////
// Code Segment                                                              //
///////////////////////////////////////////////////////////////////////////////
.text
.global     UartInitializeMSM, UartTransmitMSM, UartTransmitBufferMSM
.align      4
.arm



///////////////////////////////////////////////////////////////////////////////
// UartInitialize -- Initialize UART Hardware Unit
//
// Input:
//  R0      -- Base address of UART Unit (FFUART, BTUART, or STUART)
//
// Output:
//  None
//
// Modified:
//  R0
//  R1
//
UartInitializeMSM:


    // Save base address
    str     r0, __UartBaseAddressMSM


    // Disable Std UART interrupts
    add     r0, r0, #0x04                       // Base + 0x04, MSM8x50_UART_IER
    eor     r1, r1, r1                          // R1 = 0
    orr     r1, r1, #MSM8x50_UART_UUE            // Enable Std UART unit
    str     r1, [r0]                            // Write register
    

    // DLAB = 1
    sub     r0, r0, #0x08                       // Base + 0x0C, MSM8x50_UART_LCR
    ldr     r1, [r0]
    orr     r1, r1, #MSM8x50_UART_DLAB           //
    str     r1, [r0]                            // DLAB = 1


    // Set Baudrate = 115200 bps
    sub     r0, r0, #0x0C                       // Base + 0x00, MSM8x50_FFUART_DLL
    ldr     r1, =MSM8x50_BAUD_115200
    str     r1, [r0], #0x04                     // Low byte of Divisor, Post Index
    eor     r1, r1, r1                          // R1 = 0
    str     r1, [r0]                            // High byte of Divisor, MSM8x50_FFUART_DLH, Base + 0x04


    // DLAB = 0, set 8-N-1
    ldr     r0, =MSM8x50_FFUART_LCR
    add     r0, r0, #0x08                       // Base + 0x0C, MSM8x50_UART_LCR
    ldr     r1, [r0]
    bic     r1, r1, #MSM8x50_UART_DLAB            // DLAB = 0
    orr     r1, r1, #MSM8x50_UART_WLS8           // 8-bit, no parity, 1 stop bit
    str     r1, [r0]


    // Enable FIFO, 1 byte
    sub     r0, r0, #0x04                       // Base + 0x08, MSM8x50_UART_FCR
    ldr     r1, =( MSM8x50_UART_TRFIFOE | MSM8x50_UART_ITL1 )
    str     r1, [r0]


    // Return
    bx      lr



///////////////////////////////////////////////////////////////////////////////
// UartTransmit -- Transmit one byte via UART
//
// Input:
//  R1      -- Byte data to transmit
//
// Output:
//  None
//
// Modified:
//  R0
//  R1
//
UartTransmitMSM:


    // Send a byte
    ldr     r0, __UartBaseAddressMSM     // Load base address
    strb    r1, [r0], #MSM8x50_UART_LSR  // Send a byte and offset to 0x14


_WaitTrans:


    // Read flags
    ldr     r1, [r0]


    // Check for Transmit Data Request
    tst     r1, #MSM8x50_UART_TDRQ
    bcc     _WaitTrans


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
UartTransmitBufferMSM:


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
__UartBaseAddressMSM:
    .long   0




