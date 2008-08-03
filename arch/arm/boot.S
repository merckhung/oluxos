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



    // Initialize UART
    ldr     r0, =PXA27X_FFUART_BASE
    bl      UartInitialize


    // Display boot message
    ldr     r2, =__BootMsg
    bl      UartTransmitBuffer




    ///////////////////////////////////////////////////////////////////////////
    // Scrub Internal Memory                                                 //
    ///////////////////////////////////////////////////////////////////////////


    // Base Address
    ldr     r0, =PXA27X_INTMEM_BASE

    // Pattern
    ldr     r1, =0x12345678

    // Counter
    ldr     r2, =PXA27X_INTMEM_SIZE


_Next:


    // Postindex
    str     r1, [r0], #0x04


    // Decrease counter
    subs    r2, r2, #0x04


    // Jump if not zero
    bpl     _Next
 

    // Stop CPU Here
    b       .



///////////////////////////////////////////////////////////////////////////////
// Data Section                                                              //
///////////////////////////////////////////////////////////////////////////////
__BootMsg:
    .ascii  "Copyright (c) 2008 Olux Organization. All rights reserved.\r\n"
    .ascii  "Intel XScale PXA27x ARM Processor\r\n"
    .byte   0






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


