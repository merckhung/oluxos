/*
 * Copyright (C) 2006 -  2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: gdb.c
 * Description:
 * 	OluxOS IA32 Remote GDB Routines
 *
 */
#include <clib.h>
#include <driver/serial.h>
#include <ia32/debug.h>
#include <ia32/gdb.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/platform.h>
#include <types.h>

ExternIRQHandler(4);

static int8_t GdbInBuf[GDB_BUF_LEN];
static int8_t GdbOutBuf[GDB_BUF_LEN];

extern uint32_t SavedAllRegs[GDB_NUM_REGS];
extern uint32_t SavedErrorCode;

static int8_t* ExcStr[] = {

    "Divide Error",                   // 0
    "Debug Error",                    // 1
    "NMI",                            // 2
    "Breakpoint",                     // 3
    "Overflow",                       // 4
    "Bound Range Exceeded",           // 5
    "Invalid Opcode",                 // 6
    "Device Not Available",           // 7
    "Double Fault",                   // 8
    "Coprocessor Segment Overrun",    // 9
    "Invalid TSS",                    // 10
    "Segment Not Present",            // 11
    "Stack Fault",                    // 12
    "General Protection Fault",       // 13
    "Page Fault",                     // 14
    "N/A",                            // 15
    "x87 FPU Floating Point Error",   // 16
    "Alignment Check Exception",      // 17
    "Machine Check Exception",        // 18
    "SIMD Floating-Point Exception",  // 19
};

static uint32_t ExcTranTbl[][2] = {

    {0, 8},   {1, 5},   {2, 5},   {3, 5},  {4, 16},  {5, 16},
    {6, 4},   {7, 8},   {8, 7},   {9, 11}, {10, 11}, {11, 11},
    {12, 11}, {13, 11}, {14, 11}, {16, 7}, {~0, 7}};

void GdbInit(void) {
#ifdef CONFIG_SERIAL
  // Initialize serial port and enable interrupt
  SrInit();
  SrInitInterrupt();
#endif

  // Disable interrupt
  IntDisable();

  // Setup GDB interrupt handler
  IntRegInterrupt(IRQ_SERIAL0, IRQHandler(4), GdbSerialIntHandler);

  // Enable interrupt
  IntEnable();
}

void GdbPutChar(int8_t c) {
#ifdef CONFIG_SERIAL
  SrPutChar(c);
#endif
}

int8_t GdbGetChar(void) {
#ifdef CONFIG_SERIAL
  return SrGetChar();
#else
  return NULL;
#endif
}

int8_t* GdbGetPacket(int8_t* buf) {
  int8_t c;
  uint8_t cksm, xcksm;
  uint32_t i;

  while (1) {
    do {
      c = GdbGetChar();
    } while (c != '$');

    for (i = 0, cksm = 0; i < GDB_BUF_LEN; i++) {
      // Receive character
      c = GdbGetChar();
      if (c == '#') {
        // End of packet
        break;
      }

      // Restart buffer
      if (c == '$') {
        i = 0;
        cksm = 0;
        continue;
      }

      // Update checksum
      cksm += c;

      // Write to buffer
      buf[i] = c;
    }

    // Write NULL to the end
    buf[i] = '\0';

    if (c == '#') {
      c = GdbGetChar();
      xcksm = CbAsciiToBin(c) << 4;
      c = GdbGetChar();
      xcksm = CbAsciiToBin(c);

      // Validate checksum
      if (xcksm != cksm) {
        // Checksum failed
        GdbPutChar('-');
      } else {
        // Checksum Ok
        GdbPutChar('+');

        // Reply sequence ID
        if (buf[2] == ':') {
          GdbPutChar(buf[0]);
          GdbPutChar(buf[1]);

          return (buf + 3);
        }

        return buf;
      }
    }
  }
}

void GdbSendPacket(int8_t* buf) {
  uint8_t cksm;
  uint32_t i;

  do {
    // Header
    GdbPutChar('$');

    // Content
    for (i = 0, cksm = 0; buf[i]; i++) {
      GdbPutChar(buf[i]);
      cksm += buf[i];
    }

    // Tail
    GdbPutChar('#');

    // Checksum
    GdbPutChar(CbBinToAscii(cksm >> 4, LOWERCASE));
    GdbPutChar(CbBinToAscii(cksm & 0x0F, LOWERCASE));

  } while (GdbGetChar() != '+');
}

uint8_t TranslateException(uint32_t ExceptionVector) {
  uint8_t i;

  for (i = 0;; i++) {
    // Not found
    if ((ExcTranTbl[i][0] == ~0) || (ExceptionVector == ExcTranTbl[i][0])) {
      return ExcTranTbl[i][1] & 0xFF;
    }
  }

  return 7;
}

uint32_t ConvertAllRegsToASCII(uint32_t* arr, uint8_t count, int8_t* buf) {
  int8_t* pbuf = buf;

  for (; count--; arr++) {
    pbuf += CbBinToAsciiBuf(*arr, pbuf, LOWERCASE, 0, 0);
  }

  return (pbuf - buf);
}

void ConvertASCIIToAllRegs(uint32_t* arr, uint8_t count, int8_t* buf) {
  int8_t* pbuf = buf;
  int8_t tmp[9];

  for (; count--; arr++, pbuf += 8) {
    CbStrCpy(tmp, pbuf, 8);
    *arr = CbAsciiBufToBin(tmp);
  }
}

void GdbExceptionHandler(uint32_t ExceptionVector) {
  int8_t *p = GdbOutBuf, semicolon = ';', colon = ':';
  uint8_t sigval, step;

  DbgPrint("Catched Exception No: %d\n", ExceptionVector);
  if (ExceptionVector < 20) {
    DbgPrint("%s\n", ExcStr[ExceptionVector]);
  }
  DbgPrint("Error Code: 0x%8.8X\n", SavedErrorCode);

  // Translate Exception to Signal
  sigval = TranslateException(ExceptionVector);
  DbgPrint("sigval : %d\n", sigval);

  // Signal
  *p++ = 'T';
  *p++ = CbBinToAscii(sigval >> 4, LOWERCASE);
  *p++ = CbBinToAscii(sigval & 0x0F, LOWERCASE);

  // ESP
  *p++ = CbBinToAscii(ESP, LOWERCASE);
  *p++ = colon;
  p += CbBinToAsciiBuf(SavedAllRegs[ESP], p, LOWERCASE, 0, 0);
  *p++ = semicolon;

  // EBP
  *p++ = CbBinToAscii(EBP, LOWERCASE);
  *p++ = colon;
  p += CbBinToAsciiBuf(SavedAllRegs[EBP], p, LOWERCASE, 0, 0);
  *p++ = semicolon;

  // ESP
  *p++ = CbBinToAscii(EIP, LOWERCASE);
  *p++ = colon;
  p += CbBinToAsciiBuf(SavedAllRegs[EIP], p, LOWERCASE, 0, 0);
  *p++ = semicolon;

  // Terminate Char
  *p++ = '\0';

  // Send Packet
  GdbSendPacket(GdbOutBuf);

  // Default step = 0
  step = 0;

  while (1) {
    // Clear output buffer
    GdbOutBuf[0] = '\0';

    // Receive packet
    p = GdbGetPacket(GdbInBuf);

    switch (*p++) {
      case '?':

        GdbOutBuf[0] = 'S';
        GdbOutBuf[0] = CbBinToAscii(sigval >> 4, LOWERCASE);
        GdbOutBuf[0] = CbBinToAscii(sigval & 0x0F, LOWERCASE);
        GdbOutBuf[0] = '\0';
        break;

      // Debug Flag
      case 'd':

        DbgPrint("Remote GDB OP=d, Not Implemented\n");
        break;

      // Return CPU registers
      case 'g':

        ConvertAllRegsToASCII(SavedAllRegs, GDB_NUM_REGS, p);
        break;

      // Set CPU registers
      case 'G':

        ConvertASCIIToAllRegs(SavedAllRegs, GDB_NUM_REGS, p);
        GdbOutBuf[0] = 'O';
        GdbOutBuf[1] = 'K';
        GdbOutBuf[2] = '\0';
        break;

      // Set one CPU register
      case 'P':

        DbgPrint("Set one CPU Reg: %s\n", p);
        break;

      // Read Memory
      case 'm':

        DbgPrint("Read Memory: %s\n", p);
        break;

      // Write Memory
      case 'M':

        DbgPrint("Write Memory: %s\n", p);
        break;

      // Step one instruction
      case 's':

        step = 1;

      // Continue run
      case 'c':

        DbgPrint("Step/Continue: %s\n", p);
        break;

      // Kill the program
      case 'k':

        DbgPrint("Remote GDB OP=k, Not Implemented\n");
        break;

      default:

        DbgPrint("Remote GDB: Unrecognized Operation\n");
        break;
    }

    // Send packet, reply the request
    GdbSendPacket(GdbOutBuf);
  }
}

void GdbSerialIntHandler(uint8_t IrqNum) {
  DbgPrint("GDB Serial Int Handler\n");
  GdbExceptionHandler(0x03);
}
