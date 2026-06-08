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
#include <x86_64/debug.h>
#include <x86_64/interrupt.h>
#include <x86_64/io.h>
#include <x86_64/platform.h>
#include <x86_64/task.h>
#include <x86_64/timer.h>
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
