/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
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


void TcPrint( const __s8 *format, ... );
__s32 TcCalDigit( const __s8 *p, __s32 *digit );
void TcClear( void );
void TcCursorSet( __u8 x, __u8 y );
void TcPutchar( __s8 c );
void TcRollUp( __u8 lines );


