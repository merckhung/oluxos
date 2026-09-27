/*
 * Copyright (C) 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: olux.h
 * Description:
 *  None
 *
 */

#include "otypes.h"

//
// Definitions
//
#define OLUX_VERSION "0.1"
#define OLUX_STD_IN 0
#define OLUX_STD_OUT 1
#define OLUX_STD_ERR 2
#define OLUX_CLEAR_SCREEN "\033[2J\033[f"
#define OLUX_SZ_SECTOR 512
#define OLUX_MAX_PATHNAME 100

//
// Macros
//
#define OLUX_GET_BIT(val, bit) ((val & (1 << bit)) >> bit)
#define OLUX_BIT_MASK(x) (1 << x)
#define OLUX_ARRAY_NRCELL(x) (sizeof(x) / sizeof(x[0]))
#define OLUX_ENUM_TOSTR(NAME) #NAME

//
// Prototypes
//
int32_t CbPower(int32_t x, int32_t y);
int8_t CbAsciiToBin(int8_t value);
uint32_t CbAsciiBufToBin(const int8_t* buf);
bool ParseOneParameter(int8_t* buf, uint32_t* first);
bool ParseTwoParameters(int8_t* buf, uint32_t* first, uint32_t* second);
int8_t ConvertDWordToByte(uint32_t* Data, uint32_t Offset);
void DumpData(int8_t* pBuf, uint32_t size, uint32_t base);
void DisplayInBits(uint32_t value);
void ClrScr(void);
int8_t NonBlockReadKey(void);
bool ReadLine(int8_t* Buf, uint32_t Length);
int8_t GetKey(void);
