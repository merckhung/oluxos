/*
 * Copyright (C) 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: olux.h
 * Description:
 *  None
 *
 */



//
// Definitions
//
#define OLUX_VERSION				"0.1"
#define OLUX_STD_IN					0
#define OLUX_STD_OUT				1
#define OLUX_STD_ERR				2
#define OLUX_CLEAR_SCREEN			"\033[2J\033[f"
#define OLUX_SZ_SECTOR				512
#define OLUX_MAX_PATHNAME			100



//
// Macros
//
#define OLUX_GET_BIT( val, bit )	((val & (1 << bit)) >> bit)
#define OLUX_BIT_MASK( x )			(1 << x)
#define OLUX_ARRAY_NRCELL( x )		(sizeof(x)/sizeof(x[0]))
#define OLUX_ENUM_TOSTR( NAME )		#NAME



//
// Prototypes
//
s32 CbPower( s32 x, s32 y );
s8 CbAsciiToBin( s8 value );
u32 CbAsciiBufToBin( const s8 *buf );
bool ParseOneParameter( s8 *buf, u32 *first );
bool ParseTwoParameters( s8 *buf, u32 *first, u32 *second );
s8 ConvertDWordToByte( u32 *Data, u32 Offset );
void DumpData( s8 *pBuf, u32 size, u32 base );
void DisplayInBits( u32 value );
void ClrScr( void );
s8 NonBlockReadKey( void );
bool ReadLine( s8 *Buf, u32 Length );
s8 GetKey( void );



