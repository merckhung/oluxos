/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */
#ifdef KERNEL_DEBUG
#include <driver/console.h>
#endif

#ifdef KERNEL_DEBUG
#define DbgPrint(msg, args...) TcPrint(msg, ##args);
#else
#define DbgPrint(msg, args...)
#endif

#ifdef KERNEL_DEBUG
#define DbgStop() __asm__ __volatile__("jmp		.")
#else
#define DbgStop()
#endif

#ifdef KERNEL_DEBUG
typedef struct {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;

  uint32_t esi;
  uint32_t edi;
  uint32_t ebp;
  uint32_t esp;

  uint32_t efl;

  uint16_t cs;
  uint16_t ds;
  uint16_t es;
  uint16_t fs;
  uint16_t gs;
  uint16_t ss;

  uint64_t gdt;

  uint64_t ldt;

  uint64_t idt;

  uint32_t cr0;
  uint32_t cr2;
  uint32_t cr3;
  uint32_t cr4;

} DbgRegs_t;

void DbgDumpRegs(void);
#endif
