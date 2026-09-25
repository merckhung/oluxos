/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: clib.h
 * Description:
 *  Kernel C library header file
 *
 */

//
// Definitions
//
#include <types.h>
#define FMT_MAX_DIG 8

enum {

  LOWERCASE = 0,
  UPPERCASE
};

//
// Memory/String Routines
//
void* CbMemSet(void* mem, uint8_t ch, uint32_t sz);
void* CbMemCpy(void* dest, const void* src, uint32_t sz);
uint32_t CbStrLen(const int8_t* str);
int8_t* CbStrCpy(int8_t* dest, const int8_t* src, uint32_t sz);
int32_t CbStrCmp(const int8_t* dest, const int8_t* src, uint32_t sz);
int32_t CbStrCmpL(const int8_t* dest, const int8_t* src);
int8_t* CbStrCat(int8_t* dest, const int8_t* src, int32_t sz);
int8_t* CbIndex(const int8_t* buf, const int8_t ch);

//
// ASCII Routines
//
int8_t CbBinToAscii(int8_t value, int8_t upper);
uint32_t CbBinToAsciiBuf(uint32_t value, int8_t* buf, int8_t upper,
                         uint32_t digit, uint32_t pad);
int8_t CbAsciiToBin(int8_t value);
uint32_t CbAsciiBufToBin(const int8_t* buf);
uint32_t CbBinToBcd(uint32_t value);
uint32_t CbBcdToBin(uint32_t value);

//
// Math Routines
//
int32_t CbPower(int32_t x, int32_t y);

//
// General Routines
//
uint32_t CbParseFormat(const int8_t* fmt, uint32_t* digit, uint32_t* pad,
                       int8_t* fc);
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)

int32_t CbFmtPrint(int8_t* buf, uint32_t sz, const int8_t* format,
                   va_list args);

//
// CRC Routines
//
uint32_t CRC32(int8_t* Buf, uint32_t Len);
uint16_t CRC16(int8_t* Buf, uint32_t Len);
