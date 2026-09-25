/*
 * Copyright (C) 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * routines.S -- OluxOS routine code for
 *               Intel XScale PXA27x Processor
 *
 */


#include "pxa27x.h"


.file       "routine.S"


///////////////////////////////////////////////////////////////////////////////
// Code Segment                                                              //
///////////////////////////////////////////////////////////////////////////////
.text
.global     RtScrubMemory
.align      4
.arm



///////////////////////////////////////////////////////////////////////////////
// RtScrubMemory -- Scrub Memory with Pattern
//
// Input:
//  R0      -- Base address of buffer
//  R1      -- Pattern
//  R2      -- Size of buffer
//
// Output:
//  None
//
// Modified:
//  R0
//  R1
//  R2
//
RtScrubMemory:


_RtScrubMemoryNext:


    // Postindex
    str     r1, [r0], #0x04


    // Decrease counter
    subs    r2, r2, #0x04


    // Jump if not zero
    bne     _RtScrubMemoryNext


    // Return
    bx      lr


    

///////////////////////////////////////////////////////////////////////////////
// Data Section                                                              //
///////////////////////////////////////////////////////////////////////////////




