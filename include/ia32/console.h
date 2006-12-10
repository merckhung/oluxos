/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     VIDEO_TEXT_ADDR     0xb8000

#define     COLUMN      80
#define     LINE        25

#define     CRTC_ADDR   0x3d4
#define     CRTC_DATA   0x3d5


void ia32_TcPrint( const __s8 *format, ... );
void ia32_TcClear( void );
void ia32_TcCursorSet( __u8 x, __u8 y );
void ia32_TcPutChar( __s8 c );
void ia32_TcRollUp( __u8 lines );


