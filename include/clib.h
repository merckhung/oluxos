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
#define NULL            0
#define FMT_MAX_DIG     8



enum {

    LOWERCASE = 0,
    UPPERCASE
};



//
// Memory/String Routines
//
void *CbMemSet( void *mem, u8 ch, u32 sz );
void *CbMemCpy( void *dest, const void *src, u32 sz );
u32 CbStrLen( const s8 *str );
s8 *CbStrCpy( s8 *dest, const s8 *src, u32 sz );
s32 CbStrCmp( const s8 *dest, const s8 *src, u32 sz );
s8 *CbIndex( const s8 *buf, const s8 ch );



//
// ASCII Routines
//
s8 CbBinToAscii( s8 value, s8 upper );
u32 CbBinToAsciiBuf( u32 value, s8 *buf, s8 upper, u32 digit, u32 pad );
s8 CbAsciiToBin( s8 value );
u32 CbAsciiBufToBin( const s8 *buf );
u32 CbBinToBcd( u32 value );
u32 CbBcdToBin( u32 value );



//
// Math Routines
//
s32 CbPower( s32 x, s32 y );



//
// General Routines
//
u32 CbParseFormat( const s8 *fmt, u32 *digit, u32 *pad, s8 *fc );
s32 CbFmtPrint( s8 *buf, u32 sz, const s8 *format, const s8 **args );



