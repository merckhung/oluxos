/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: page.c
 * Description:
 * 	OluxOS IA32 paging routines
 *
 */
#include <x86_64/bios.h>
#include <x86_64/debug.h>
#include <x86_64/page.h>
#include <x86_64/platform.h>
#include <types.h>

volatile uint32_t* PDEPtr = (volatile uint32_t*)PDE_ADDR;
volatile uint32_t* PTEPtr = (volatile uint32_t*)PTE_ADDR;

volatile uint8_t* e820_count = (volatile uint8_t*)E820_COUNT;
volatile E820Result* e820_base = (volatile E820Result*)E820_BASE;
static uint64_t MemSize = 0;

static const char* AddrType[] = {

    "Undefined", "Memory", "Reserved", "ACPI", "NVS", "Unusable",
};

//
// MmPageInit
//
// Input:
//  None
//
// Output:
//  None
//
// Description:
//  Initialize page directories and page tables
//
void MmPageInit(void) {
  int32_t i;

  // Print E820 Information
  for (i = 0; i < *e820_count; i++) {
    if ((e820_base + i)->RecType == ADDRESS_RANGE_MEMORY) {
      MemSize += (((uint64_t)(e820_base + i)->LengthHigh) << 32ULL);
      MemSize += ((uint64_t)(e820_base + i)->LengthLow);
    }
  }

  // Display total memory size
  MmShowE820Info();

  // x86_64 Long Mode paging is already configured in setup.S.
  // We do not need to rebuild 32-bit page tables here.
}

void MmShowE820Info(void) {
  int32_t i;

  // Display total memory size
  TcPrint("Total Memory Size: %d MB\n",
          (uint32_t)(MemSize / 1024ULL / 1024ULL));

  // Print E820 Information
  for (i = 0; i < *e820_count; i++) {
    TcPrint("E820: 0x%8.8X_%8.8X - 0x%8.8X_%8.8X <%s>\n",
            (e820_base + i)->BaseAddrHigh, (e820_base + i)->BaseAddrLow,
            (e820_base + i)->BaseAddrHigh + (e820_base + i)->LengthHigh,
            (e820_base + i)->BaseAddrLow + (e820_base + i)->LengthLow - 1,
            MmShowE820Type((e820_base + i)->RecType));
  }
}

const char* MmShowE820Type(E820Type RecType) {
  if (RecType < ADDRESS_RANGE_MEMORY || RecType > ADDRESS_RANGE_NVS) {
    return AddrType[ADDRESS_RANGE_UNDEFINED];
  }

  return AddrType[RecType];
}

void MmEnablePaging(volatile uint32_t* Ptr) {
}

void MmDisablePaging(void) {
}

void MmEnablePSE(void) {
}

void MmDisablePSE(void) {
}

void MmPageFaultHandler(uint32_t ErrorCode, GeneralRegisters* Regs) {
  DbgPrint("Page Fault (14)\nRIP: 0x%8.8X, CS: 0x%4.4X\n", Regs->rip, Regs->cs);

  // Present or not present
  if (ErrorCode & PF_PRESENT) {
    DbgPrint("Present: Yes\n");
  } else {
    DbgPrint("Present: No\n");
  }

  // Read or write
  if (ErrorCode & PF_RW) {
    DbgPrint("Access:  Write\n");
  } else {
    DbgPrint("Access:  Read\n");
  }

  // Supervisor or user mode
  if (ErrorCode & PF_MODE) {
    DbgPrint("Mode:    User\n");
  } else {
    DbgPrint("Mode:    Supervisor\n");
  }

  // RSVD or not
  if (ErrorCode & PF_RSVD) {
    DbgPrint("RSVD:    Yes\n");
  } else {
    DbgPrint("RSVD:    No\n");
  }
}
