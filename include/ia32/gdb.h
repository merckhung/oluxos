/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */

//
// Definitions
//
#define GDB_BUF_LEN 400
#define GDB_NUM_REGS 16

enum {

  EAX,
  ECX,
  EDX,
  EBX,
  ESP,
  EBP,
  ESI,
  EDI,
  EIP,
  EFLAGS,
  CS,
  SS,
  DS,
  ES,
  FS,
  GS
};

//
// Prototypes
//
void GdbInit(void);
void GdbPutChar(int8_t c);
int8_t GdbGetChar(void);
int8_t* GdbGetPacket(int8_t* buf);
void GdbSendPacket(int8_t* buf);
uint8_t TranslateException(uint32_t ExceptionVector);
void GdbExceptionHandler(uint32_t ExceptionVector);
void GdbSerialIntHandler(uint8_t IrqNum);
