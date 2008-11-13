/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: types.h
 * Description:
 * 	None
 *
 */


typedef     unsigned char           u8;
typedef     unsigned short int      u16;
typedef     unsigned int            u32;
typedef     unsigned long long int  u64;

typedef     char                    s8;
typedef     short int               s16;
typedef     int                     s32;
typedef     long long int           s64;

typedef     u8                    	uint8;
typedef     u16                   	uint16;
typedef     u32                   	uint32;
typedef     u64                   	uint64;

typedef     s8                    	int8;
typedef     s16                   	int16;
typedef     s32                   	int32;
typedef     s64                   	int64;

typedef     u8                      bool;
typedef		volatile void			*PhysicalMemory;


#define     FALSE                   0
#define     TRUE                    1
#define		NULL					0


#define     PACKED                  __attribute__((packed))



