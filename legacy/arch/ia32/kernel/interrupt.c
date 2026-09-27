/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * interrupt.c -- OluxOS IA32 interrupt routines
 *
 */
#include <clib.h>
#include <ia32/debug.h>
#include <ia32/i8259.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/platform.h>
#include <types.h>

IntHandlerLst InterrupHandlertList[NR_VECTOR];
IDTEntry IDTTable[NR_VECTOR];
IDTPtr IDTPointer;

extern void __KERNEL_CS(void);
extern void InterruptHandler(void);
extern void ExceptionHandler(void);

extern void divide_error(void);
extern void debug(void);
extern void nmi(void);
extern void breakpoint(void);
extern void overflow(void);
extern void bound_range_exceeded(void);
extern void invalid_opcode(void);
extern void device_not_available(void);
extern void double_fault(void);
extern void coprocessor_segment_overrun(void);
extern void invalid_tss(void);
extern void segment_not_present(void);
extern void stack_fault(void);
extern void general_protection_exception(void);
extern void PageFaultHandler(void);
extern void x87_fpu_floating_point_error(void);
extern void alignment_check_exception(void);
extern void machine_check_exception(void);
extern void simd_floating_point_exception(void);

//
// IntInitInterrupt
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Initialize CPU IDT tables
//
void IntInitInterrupt(void) {
  uint32_t i;

  // DbgPrint( "IDT Entry Size: 0x%8.8X\n", sizeof( IDTEntry ) );

  // Initialize IDT Tables
  CbMemSet((int8_t*)IDTTable, 0, sizeof(IDTEntry) * NR_VECTOR);

  // Setup exceptions handler
  for (i = 0; i < HW_INT_START; i++) {
    IntSetIDT(i, ExceptionHandler, __KERNEL_CS, GATE_TRAP_FLAG);
  }

  // Initialize IDT Entry for Exceptions
  IntSetIDT(0, divide_error, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(1, debug, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(2, nmi, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(3, breakpoint, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(4, overflow, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(5, bound_range_exceeded, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(6, invalid_opcode, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(7, device_not_available, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(8, double_fault, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(9, coprocessor_segment_overrun, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(10, invalid_tss, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(11, segment_not_present, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(12, stack_fault, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(13, general_protection_exception, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(14, PageFaultHandler, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(16, x87_fpu_floating_point_error, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(17, alignment_check_exception, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(18, machine_check_exception, __KERNEL_CS, GATE_TRAP_FLAG);
  IntSetIDT(19, simd_floating_point_exception, __KERNEL_CS, GATE_TRAP_FLAG);

  // Setup IDT Pointer
  IDTPointer.Limit = (NR_VECTOR - 1) * sizeof(IDTEntry);
  IDTPointer.BaseAddr = (uint32_t)IDTTable;
  // DbgPrint( "IDTPointer = 0x%X, IDTTable = 0x%X\n", &IDTPointer, IDTTable );

  // Load IDT Pointer
  IntLoadIDTRegister(&IDTPointer);

  // Initialize i8259A PIC
  i8259Init();

  // Enable interrupt
  // DbgPrint( "Enable Interrupt\n" );
  IntEnable();
}

void IntLoadIDTRegister(IDTPtr* Ptr) {
  // Load IDT Register
  __asm__ __volatile__(

      "lidt   (%%eax)\n" ::"a"(Ptr));
}

//
// IntSetIDT
//
// Input:
//  index   : Interrupt number
//  offset  : Offset of interrupt handler from code segment
//  seg     : Segment descriptor offset
//  flag    : Flags of Gate
//
// Return:
//  None
//
// Description:
//  Setup IDT entry for specified interrupt/exception number
//
void IntSetIDT(uint32_t Index, void* Handler, void* SegSel, uint8_t Flags) {
  uint32_t Offset = (uint32_t)Handler;

  IDTTable[Index].OffsetLSW = (uint16_t)(Offset & 0xFFFF);
  IDTTable[Index].OffsetMSW = (uint16_t)((Offset >> 16) & 0xFFFF);
  IDTTable[Index].SegSelect = (uint32_t)SegSel;
  IDTTable[Index].Flags = Flags;
}

//
// IntDelIDT
//
// Input:
//  index   : Interrupt number
//
// Return:
//  None
//
// Description:
//  Delete IDT entry for specified interrupt/exception number
//
void IntDelIDT(uint32_t Index) {
  CbMemSet((int8_t*)(&IDTTable[Index]), 0, sizeof(IDTEntry));
}

//
// IntDisable
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Disable CPU interrupt
//
void IntDisable(void) { __asm__("cli\n"); }

//
// IntEnable
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Enable CPU interrupt
//
void IntEnable(void) { __asm__("sti"); }

//
// IntRegInterrupt
//
// Intput:
//  intnum      : Interrupt vector number
//  handler     : offset address of interrupt handler
//
// Return:
//  0  -- success
//  -1 -- failed
//
// Description:
//  Public routine for CPU interrupt register
//
void IntRegInterrupt(uint32_t IrqNum, void* IrqHandler,
                     void (*HwIntHandler)(uint8_t IrqNum)) {
  DbgPrint("IntRegInterrupt: IRQ 0x%x, Comm 0x%x, Hw 0x%x\n", IrqNum,
           (uint32_t)IrqHandler, (uint32_t)HwIntHandler);

  // Disable interrupt
  IntDisable();

  // Add interrupt gate
  IntSetIDT(IrqNum + HW_INT_START, (void*)IrqHandler, __KERNEL_CS,
            GATE_INT_FLAG);

  // Install interrupt handler
  InterrupHandlertList[IrqNum].IrqHandler = HwIntHandler;

  // Enable 8259A IRQ line
  i8259EnableIRQ(IrqNum);

  // Enable interrupt
  IntEnable();
}

//
// IntUnregInterrupt
//
// Input:
//  intnum      : Interrupt vector number
//
// Return:
//  None
//
// Description:
//  Public routine for CPU interrupt register
//
void IntUnregInterrupt(uint32_t IrqNum) {
  DbgPrint("IntUnregInterrupt: Index 0x%x\n", IrqNum);

  // Disable interrupt
  IntDisable();

  // Disable 8259A IRQ line
  i8259DisableIRQ(IrqNum);

  // Uninstall interrupt handler
  InterrupHandlertList[IrqNum].IrqHandler = 0;

  // Delete interrupt gate
  IntDelIDT(IrqNum + HW_INT_START);

  // Enable interrupt
  IntEnable();
}

//
// IntHandleIRQ
//
// Input:
//  irqnum      : IRQ number
//
// Output:
//  None
//
// Description:
//  IRQ handler
//
void IntHandleIRQ(uint32_t IrqNum, GeneralRegisters* Regs) {
  // Disable interrupt
  IntDisable();

  // Relinquish to hardware interrupt handler
  InterrupHandlertList[IrqNum].IrqHandler(IrqNum);

  // Enable interrupt
  IntEnable();

  // Issue End Of Interrupt (EOI)
  IntIssueEOI();
}

//
// IntIssueEOI
//
// Input:
//  None
//
// Output:
//  None
//
// Description:
//  Issue End of Interrupt action
//
void IntIssueEOI(void) { i8259IssueEOI(); }

void IntShowIDTTable(void) {
  uint32_t i;
  int32_t* p;

  for (i = 0x20; i < 0x2F; i++) {
    p = (int32_t*)&IDTTable[i];
    DbgPrint("Interrupt Number: %d\n", i);
    DbgPrint("0x%8.8X%8.8X\n", *(p + 1), *p);
  }
}
