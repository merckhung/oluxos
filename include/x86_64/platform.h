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
  uint64_t rax; // 0
  uint64_t rbx; // 8
  uint64_t rcx; // 16
  uint64_t rdx; // 24
  uint64_t rsi; // 32
  uint64_t rdi; // 40
  uint64_t rbp; // 48
  uint64_t rsp; // 56
  uint64_t r8;  // 64
  uint64_t r9;  // 72
  uint64_t r10; // 80
  uint64_t r11; // 88
  uint64_t r12; // 96
  uint64_t r13; // 104
  uint64_t r14; // 112
  uint64_t r15; // 120

  uint64_t rip; // 128
  uint64_t rflags; // 136

  uint16_t cs;
  uint16_t rvsd0;
  uint16_t ss;
  uint16_t rvsd1;
  uint16_t ds;
  uint16_t rvsd2;
  uint16_t es;
  uint16_t rvsd3;
  uint16_t fs;
  uint16_t rvsd4;
  uint16_t gs;
  uint16_t rvsd5;
} GeneralRegisters;
#endif
#endif  // __PLATFORM_H__
