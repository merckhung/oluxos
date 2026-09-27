/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */

typedef struct {
  uint64_t cr3;
  uint64_t rip;
  uint64_t rflags;
  uint64_t rax;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rbx;
  uint64_t rsp;
  uint64_t rbp;
  uint64_t rsi;
  uint64_t rdi;
  uint16_t es;
  uint16_t cs;
  uint16_t ss;
  uint16_t ds;
  uint16_t fs;
  uint16_t gs;
} Task_t;

typedef struct {
  uint32_t reserved0;
  uint64_t rsp0;
  uint64_t rsp1;
  uint64_t rsp2;
  uint64_t reserved1;
  uint64_t ist1;
  uint64_t ist2;
  uint64_t ist3;
  uint64_t ist4;
  uint64_t ist5;
  uint64_t ist6;
  uint64_t ist7;
  uint64_t reserved2;
  uint16_t reserved3;
  uint16_t iobmp;
} __attribute__((packed)) TSS_t;

typedef struct {
  uint16_t limit0;
  uint16_t baseaddr0;
  uint8_t baseaddr1;
  uint8_t flag;
  uint8_t limit1;
  uint8_t baseaddr2;
} __attribute__((packed)) TSSD_t;

void TskInit(void);
void TskStart(void);
void TskSwitch(uint32_t origtsk, uint32_t newtsk);
void TskScheduler(void);
