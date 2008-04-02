/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define VIDEO_TEXT_ADDR     0xB8000
#define CONSOLE_BUF_LEN     1024

#define COLUMN              80
#define LINE                25

#define CRTC_ADDR           0x3D4
#define CRTC_DATA           0x3D5


void TcPrint( const s8 *format, ... );
void TcClear( void );
void TcCursorSet( u8 x, u8 y );
void TcPutChar( s8 c );
void TcRollUp( u8 lines );


