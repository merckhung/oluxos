/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     VIDEO_TEXT_ADDR     0xb8000
#define     CONSOLE_BUF_LEN     1024

#define     COLUMN      80
#define     LINE        25

#define     CRTC_ADDR   0x3d4
#define     CRTC_DATA   0x3d5


void TcPrint( const s8 *format, ... );
s32 TcCalDigit( const s8 *p, s32 *digit );
void TcClear( void );
void TcCursorSet( u8 x, u8 y );
void TcPutchar( s8 c );
void TcRollUp( u8 lines );


