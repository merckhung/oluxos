/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */

typedef struct {
  uint32_t cr3;
  uint32_t eip;
  uint32_t eflags;
  uint32_t eax;
  uint32_t ecx;
  uint32_t edx;
  uint32_t ebx;
  uint32_t esp;
  uint32_t ebp;
  uint32_t esi;
  uint32_t edi;
  uint16_t es;
  uint16_t cs;
  uint16_t ss;
  uint16_t ds;
  uint16_t fs;
  uint16_t gs;
} Task_t;

typedef struct {
  uint16_t prev_task_link;
  uint16_t reserved0;
  uint32_t esp0;
  uint16_t ss0;
  uint16_t reserved1;
  uint32_t esp1;
  uint16_t ss1;
  uint16_t reserved2;
  uint32_t esp2;
  uint16_t ss2;
  uint16_t reserved3;
  uint32_t cr3;
  uint32_t eip;
  uint32_t eflags;
  uint32_t eax;
  uint32_t ecx;
  uint32_t edx;
  uint32_t ebx;
  uint32_t esp;
  uint32_t ebp;
  uint32_t esi;
  uint32_t edi;
  uint16_t es;
  uint16_t reserved4;
  uint16_t cs;
  uint16_t reserved5;
  uint16_t ss;
  uint16_t reserved6;
  uint16_t ds;
  uint16_t reserved7;
  uint16_t fs;
  uint16_t reserved8;
  uint16_t gs;
  uint16_t reserved9;
  uint16_t ldt;
  uint16_t reserved10;
  uint16_t debug;
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
