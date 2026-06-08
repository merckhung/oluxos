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

static int8_t* AddrType[] = {

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
  uint32_t filladdr = 0;

  // Print E820 Information
  for (i = 0; i < *e820_count; i++) {
    if ((e820_base + i)->RecType == ADDRESS_RANGE_MEMORY) {
      MemSize += (((uint64_t)(e820_base + i)->LengthHigh) << 32ULL);
      MemSize += ((uint64_t)(e820_base + i)->LengthLow);
    }
  }

  // Display total memory size
  MmShowE820Info();

  // DbgPrint( "PDE Start Addr: 0x%8.8X\n", (uint32_t)PDEPtr );
  // DbgPrint( "PTE Start Addr: 0x%8.8X\n", (uint32_t)PTEPtr );

  // Initialize Page Directory Entries
  for (i = 0; i < NR_PDE; i++) {
    *(PDEPtr + i) = (PTE_ADDR + (i * PAGE_SIZE)) | P_SUP_WT_RW_4K;

    /*
if( (i + 1) == PDENUM ) {

//
// Masked for User space
//
*(PDEPtr + i) |= 0x00000004;
}
    */

    // DbgPrint( "PDE Addr = 0x%8.8X, Value = 0x%8.8X\n", (uint32_t)(PDEPtr +
    // i), *(PDEPtr + i) );
  }

  // Initialize Page Table Entries
  for (i = 0; i < NR_PTE; i++) {
    *(PTEPtr + i) = (uint32_t)(filladdr + i * PAGE_SIZE) | P_SUP_WT_RW_4K;

    // DbgPrint( "PTE Addr = 0x%8.8X, Value = 0x%8.8X\n", (uint32_t)(PTEPtr +
    // i), *(PTEPtr + i) );

    /*
if( i >= (PTENUM - 1024) ) {


//
// Masked for User space
//
*(PTEPtr + i) |= 0x00000004;
//DbgPrint( "PTE value = 0x%8X\n", *(PTEPtr + i) );

if( !usermem ) {

    //DbgPrint( "usermem 1 = 0x%8X, value 1 = 0x%8X\n", PTEPtr + i, *(PTEPtr +
i) ); usermem = PTEPtr + i;
}
}
     */
  }

  // Setup PDE pointer and enable paging
  MmEnablePaging(PDEPtr);
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

int8_t* MmShowE820Type(E820Type RecType) {
  if (RecType < ADDRESS_RANGE_MEMORY || RecType > ADDRESS_RANGE_NVS) {
    return AddrType[ADDRESS_RANGE_UNDEFINED];
  }

  return AddrType[RecType];
}

void MmEnablePaging(volatile uint32_t* Ptr) {
  uint32_t PtrAddr = (uint32_t)Ptr;

  DbgPrint("Kernel PDE Pointer Addr = 0x%8.8X\n", PtrAddr);

  // Setup Page Directory Entry Pointer (CR3)
  // Enable Paging (CR0)
  // Flush TLB
  __asm__ __volatile__(

      "movl	%0, %%eax\n"
      "movl   %%eax, %%cr3\n"
      "movl   %%cr0, %%eax\n"
      "orl    $0x80000000, %%eax\n"
      "movl   %%eax, %%cr0\n"
      "jmp	1f\n"
      "1:\n" ::"m"(PtrAddr)
      : "eax");
}

void MmDisablePaging(void) {
  // Disable Paging (CR0)
  // Flush TLB
  __asm__ __volatile__(

      "movl	%%cr0, %%eax\n"
      "andl	$0x7FFFFFFF, %%eax\n"
      "movl	%%eax, %%cr0\n"
      "xorl	%%eax, %%eax\n"
      "movl	%%eax, %%cr3\n" ::
          : "eax");
}

void MmEnablePSE(void) {
  // Enable Bit 4 of CR4
  __asm__ __volatile__(

      "movl	%%cr4, %%eax\n"
      "orl	$0x10, %%eax\n"
      "movl	%%eax, %%cr4\n" ::
          : "eax");
}

void MmDisablePSE(void) {
  // Disable Bit 4 of CR4
  __asm__ __volatile__(

      "movl   %%cr4, %%eax\n"
      "andl	$0xFFFFFFEF, %%eax\n"
      "movl   %%eax, %%cr4\n" ::
          : "eax");
}

void MmPageFaultHandler(uint32_t ErrorCode, GeneralRegisters* Regs) {
  DbgPrint("Page Fault (14)\nEIP: 0x%8.8X, CS: 0x%4.4X\n", Regs->eip, Regs->cs);

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
