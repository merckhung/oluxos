/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: console.h
 * Description:
 *  OluxOS text console driver header file
 *
 */

#define VIDEO_TEXT_ADDR 0xB8000
#define CONSOLE_BUF_LEN 1024

#define COLUMN 80
#define LINE 25

#define CRTC_ADDR 0x3D4
#define CRTC_DATA 0x3D5

void TcPrint(const int8_t* format, ...);
void TcClear(void);
void TcCursorSet(uint8_t x, uint8_t y);
void TcPutChar(int8_t c);
void TcRollUp(uint8_t lines);
