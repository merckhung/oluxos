//
// Copyright (C) 2008 Olux Organization All rights reserved.
// Author: Merck Hung <merck@olux.org>
//
// @OLUXORG_LICENSE_HEADER_START@
// @OLUXORG_LICENSE_HEADER_END@
//
// pxa25x.h -- Intel XScale PXA250/255 Processor
//

//
// See PXA25x Developer's Manual P3-37
// Coprocessor 14, Register 6
//
#define TURBO_MODE              0x00000001  // Enter Turbo Mode
#define FCS_SEQ                 0x00000002  // Enter Frequency Change Sequence


//
// See PXA25x Developer's Manual P3-38
// Coprocessor 14, Register 7
//
#define RUN_TURBO_MODE          0x00000000  // Run/Turbo Mode
#define IDLE_MODE               0x00000001  // Idle Mode
#define SLEEP_MODE              0x00000003  // Sleep Mode


//
// See ARM Architecture Reference Manual A2-9
// Program Status Registers(PSR)
// 
#define ARM_PSR_IRQ             0x00000080  // Disable Interrupt bit
#define ARM_PSR_FIQ             0x00000040  // Disable Fast Interrupt bit


#define ARM_PSR_USER_MODE       0x00000010  // CPU User Mode
#define ARM_PSR_FIQ_MODE        0x00000011  // CPU Fast Interrupt Mode
#define ARM_PSR_IRQ_MODE        0x00000012  // CPU Interrupt Mode
#define ARM_PSR_SUPERVISOR_MODE 0x00000013  // CPU Supervisor Mode
#define ARM_PSR_ABORT_MODE      0x00000017  // CPU Abort Mode
#define ARM_PSR_UNDEFINED_MODE  0x0000001b  // CPU Undefined Mode
#define ARM_PSR_SYSTEM_MODE     0x0000001f  // CPU System Mode


#define PXA25X_STUART_BASE      0x4000014C
#define PXA25X_STUART_RBR       (PXA25X_STUART_BASE + 0x00)
#define PXA25X_STUART_THR       (PXA25X_STUART_BASE + 0x00)
#define PXA25X_STUART_IER       (PXA25X_STUART_BASE + 0x04)
#define PXA25X_STUART_IIR       (PXA25X_STUART_BASE + 0x08)
#define PXA25X_STUART_FCR       (PXA25X_STUART_BASE + 0x08)
#define PXA25X_STUART_LCR       (PXA25X_STUART_BASE + 0x0C)
#define PXA25X_STUART_MCR       (PXA25X_STUART_BASE + 0x10)
#define PXA25X_STUART_LSR       (PXA25X_STUART_BASE + 0x14)
#define PXA25X_STUART_MSR       (PXA25X_STUART_BASE + 0x18)
#define PXA25X_STUART_SPR       (PXA25X_STUART_BASE + 0x1C)
#define PXA25X_STUART_ISR       (PXA25X_STUART_BASE + 0x20)
#define PXA25X_STUART_DLL       (PXA25X_STUART_BASE + 0x00)
#define PXA25X_STUART_DLH       (PXA25X_STUART_BASE + 0x04)


#define PXA25X_STUART_DMAE      0x80
#define PXA25X_STUART_UUE       0x40
#define PXA25X_STUART_NRZE      0x20
#define PXA25X_STUART_RTOIE     0x10
#define PXA25X_STUART_MIE       0x08
#define PXA25X_STUART_RLSE      0x04
#define PXA25X_STUART_TIE       0x02
#define PXA25X_STUART_RAVIE     0x01


#define PXA25X_STUART_DLAB      0x80
#define PXA25X_STUART_SB        0x40
#define PXA25X_STUART_STKYP     0x20
#define PXA25X_STUART_EPS       0x10
#define PXA25X_STUART_PEN       0x08
#define PXA25X_STUART_STB       0x04
#define PXA25X_STUART_WLS8      0x03
#define PXA25X_STUART_WLS7      0x02
#define PXA25X_STUART_WLS6      0x01
#define PXA25X_STUART_WLS5      0x00


#define PXA25X_STUART_ITL32     0x03
#define PXA25X_STUART_ITL16     0x02
#define PXA25X_STUART_ITL8      0x01
#define PXA25X_STUART_ITL1      0x00
#define PXA25X_STUART_RESETTF   0x04
#define PXA25X_STUART_RESETRF   0x02
#define PXA25X_STUART_TRFIFOE   0x01


#define PXA25X_BAUD_115200      0x08
#define PXA25X_BAUD_38400       0x18



