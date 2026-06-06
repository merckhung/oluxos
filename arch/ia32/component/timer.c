/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * timer.c -- OluxOS IA32 timer routines
 *
 */
#include <ia32/debug.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/platform.h>
#include <ia32/task.h>
#include <ia32/timer.h>
#include <types.h>

ExternIRQHandler(0);

//
// TmIntTimer
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Initialize 8253 timer chip
//
void TmInitTimer(void) {
  // Register interrupt handler
  IntRegInterrupt(IRQ_TIMER, IRQHandler(0), TmIntHandler);
}

//
// TmIntHandler
//
// Input:
//  irqnum  : IRQ number
//
// Return:
//  None
//
// Description:
//  8253 timer interrupt handler
//
void TmIntHandler(uint8_t IrqNum) {
  // volatile uint8_t *videomem = (uint8_t *)0xB84FE;

  //(*videomem)++;
  //(*(videomem + 1))++;
}
