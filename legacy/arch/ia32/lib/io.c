/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * io.c -- OluxOS IA32 Input/Output routines
 *
 */
#include <types.h>

//
// IoOutByte
//
// Input:
//  value   : Value to write
//  port    : IO port number to write
//
// Return:
//  None
//
// Description:
//  Write value in byte to IO port
//
void IoOutByte(const uint8_t value, const uint16_t port) {
  __asm__ __volatile__("outb   %%al, %%dx\n" ::"a"(value), "d"(port));
}

//
// IoInByte
//
// Input:
//  port    : IO port number to write
//
// Return:
//  IO port value in byte
//
// Description:
//  Read value in byte from IO port
//
uint8_t IoInByte(const uint16_t port) {
  uint8_t value;

  __asm__ __volatile__("inb    %%dx, %%al\n" : "=a"(value) : "d"(port));

  return value;
}

//
// IoOutWord
//
// Input:
//  value   : Value to write
//  port    : IO port number to write
//
// Return:
//  None
//
// Description:
//  Write value in word to IO port
//
void IoOutWord(const uint16_t value, const uint16_t port) {
  __asm__ __volatile__("outw   %%ax, %%dx\n" ::"a"(value), "d"(port));
}

//
// IoInWord
//
// Input:
//  port    : IO port number to write
//
// Return:
//  IO port value in word
//
// Description:
//  Read value in word from IO port
//
uint16_t IoInWord(const uint16_t port) {
  uint16_t value;

  __asm__ __volatile__("inw    %%dx, %%ax\n" : "=a"(value) : "d"(port));

  return value;
}

//
// IoOutDWord
//
// Input:
//  value   : Value to write
//  port    : IO port number to write
//
// Return:
//  None
//
// Description:
//  Write value in double word to IO port
//
void IoOutDWord(const uint32_t value, const uint16_t port) {
  __asm__ __volatile__("outl   %%eax, %%dx\n" ::"a"(value), "d"(port));
}

//
// IoInDWord
//
// Input:
//  port    : IO port number to write
//
// Return:
//  IO port value in double word
//
// Description:
//  Read value in double word from IO port
//
uint32_t IoInDWord(const uint16_t port) {
  uint32_t value;

  __asm__ __volatile__("inl   %%dx, %%eax\n" : "=a"(value) : "d"(port));

  return value;
}
