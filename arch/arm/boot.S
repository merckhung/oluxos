//
// Copyright (C) 2008 Olux Organization All rights reserved.
// Author: Merck Hung <merck@olux.org>
//
// @OLUXORG_LICENSE_HEADER_START@
// @OLUXORG_LICENSE_HEADER_END@
//
// boot.S -- OluxOS bootstrap code for
//           Intel XScale PXA27x Processor
//


#include "pxa27x.h"


.file       "boot.S"


///////////////////////////////////////////////////////////////////////////////
// Code Segment                                                              //
///////////////////////////////////////////////////////////////////////////////
.text
.global     _SystemBoot
.align      4
.arm



_SystemBoot:



    ///////////////////////////////////////////////////////////////////////////
    // Vector Table                                                          //
    ///////////////////////////////////////////////////////////////////////////
    b       _RESET_VECTOR
    ldr     pc, _UNDEFINED_VECTOR
    ldr     pc, _SWI_VECTOR
    ldr     pc, _PREFETCH_ABORT_VECTOR
    ldr     pc, _DATA_ABORT_VECTOR
    ldr     pc, _ADDR_ERROR_VECTOR
    ldr     pc, _IRQ_VECTOR
    ldr     pc, _FIQ_VECTOR



_UNDEFINED_VECTOR:
    .word   _UNDEFINED_ISR

_SWI_VECTOR:
    .word   _SWI_ISR

_PREFETCH_ABORT_VECTOR:
    .word   _PREFETCH_ABORT_ISR

_DATA_ABORT_VECTOR:
    .word   _DATA_ABORT_ISR

_ADDR_ERROR_VECTOR:
    .word   _ADDR_ERROR_ISR

_IRQ_VECTOR:
    .word   _IRQ_ISR

_FIQ_VECTOR:
    .word   _FIQ_ISR



    ///////////////////////////////////////////////////////////////////////////
    // Initialize Hardware                                                   //
    ///////////////////////////////////////////////////////////////////////////
_RESET_VECTOR:



    ldr     r0, =PXA27X_FFUART_BASE
    bl      UartInitialize


    mov     r1, #'B'
    bl      UartTransmit


    mov     r1, #'Z'
    bl      UartTransmit


    b       .



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
UartInitialize:


    // Save base address
    str     r0, UartBaseAddress


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


    // Set Baudrate = 115200bps
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
//  R1      -- Byte data to transmit
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
    ldr     r0, UartBaseAddress         // Load base address
    strb    r1, [r0]                    // Send a byte


/*
    // Offset to PXA27X_UART_LSR
    add     r0, r0, #0x14               // Base + 0x14


_WaitTrans:


    // Read flags
    ldr     r1, [r0]


    // Check for Transmit Data Request
    tst     r1, #PXA27X_UART_TDRQ
    bz      _WaitTrans
*/


    // Return
    bx      lr



UartBaseAddress:
    .long   0x0



    //
    // Setup initial exception stack
    //
    // Disable interrupt, fast interrupt bit, and enter into supervisor mode
    ldr     r0, =( ARM_PSR_IRQ | ARM_PSR_FIQ | ARM_PSR_SUPERVISOR_MODE )

    // Set current program status
    msr     cpsr, r0

    // Set base address of stack for supervisor mode
    ldr     sp, _supervisor_stack_addr

    // Manually put return address into lr
    ldr     lr, _c_rom_start_addr
    b       .


    // Base Address
    ldr     r0, =0x00000000

    // Offset
    ldr     r1, =0x00000004

    // Pattern
    ldr     r2, =0x12345678

    // Counter
    ldr     r3, =0x80


_Next:

    // Postindex
    str     r2, [r0], r1

    // Decrease counter
    subs    r3, r3, #1

    // Jump if not zero
    bpl     _Next
    b       .


_supervisor_stack_addr:
    .word   _supervisor_stack


_c_rom_start_addr:
    .word   c_rom_start


_UNDEFINED_ISR:
_SWI_ISR:
_PREFETCH_ABORT_ISR:
_DATA_ABORT_ISR:
_ADDR_ERROR_ISR:
_IRQ_ISR:
_FIQ_ISR:
_supervisor_stack:
c_rom_start:


