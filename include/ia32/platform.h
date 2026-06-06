/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: platform.h
 * Description:
 * 	None
 *
 */

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

//
// Definitions
//
#define KRN_LOAD_ADDR 0x00100000
#define KRN_BOOT_ADDR 0x7c00
#define KRN_BOOT_SEG 0x07c0
#define KRN_BLOCK_SZ 0x200
#define KRN_STACK_SZ 4096

//
// Structures
//
#ifndef __ASSEMBLER__
typedef struct PACKED _GeneralRegisters {
  uint32_t eax;  // 0
  uint32_t ecx;  // 4
  uint32_t edx;  // 8
  uint32_t ebx;  // 12

  uint32_t esp;  // 16
  uint32_t ebp;  // 20
  uint32_t esi;  // 24
  uint32_t edi;  // 28

  uint32_t eip;     // 32
  uint32_t eflags;  // 36

  uint16_t cs;  // 40
  uint16_t rvsd0;
  uint16_t ss;  // 44
  uint16_t rvsd1;
  uint16_t ds;  // 48
  uint16_t rvsd2;
  uint16_t es;  // 52
  uint16_t rvsd3;
  uint16_t fs;  // 56
  uint16_t rvsd4;
  uint16_t gs;  // 60
  uint16_t rvsd5;

} GeneralRegisters;
#endif
#endif  // __PLATFORM_H__
