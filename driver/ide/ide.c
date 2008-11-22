/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * ide.c -- OluxOS standard IDE Hard Disk driver
 *
 */
#include <types.h>
#include <ia32/platform.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/debug.h>
#include <driver/ide.h>



void IDEReadData( s8 *buf ) {

    s16 i, j, k, tmp;

    for( i = 0, j = 0, k = 0 ; i < 256 ; i++, j++, k+=2 ) {

        tmp = IoInWord( IDE_DATA );
        buf[ k ] = tmp & 0xff;
        buf[ k + 1 ] = (tmp >> 8) & 0xff;
    }
}



void IDEReadSector( u32 sector, s8 *buf ) {


	DbgPrint( "IDE Read LBA %d\n", sector );


    IoOutByte( 0x01, IDE_NSECTOR );
    IoOutByte( (sector & 0x000000ff), IDE_SECTOR );
    IoOutByte( (sector & 0x0000ff00) >> 8, IDE_LOWCYL );
    IoOutByte( (sector & 0x00ff0000) >> 16, IDE_HIGHCYL );
    IoOutByte( ((sector & 0x0f000000) >> 24) | 0xe0, IDE_SELECT );
    IoOutByte( 0x20, IDE_COMMAND );

	IDEReadData( buf );
}



void IDEWriteSector( u32 sector, s8 *buf ) {

    s32 i;

    IoOutByte( 0x01, IDE_NSECTOR );
    IoOutByte( (sector & 0x000000ff), IDE_SECTOR );
    IoOutByte( (sector & 0x0000ff00) >> 8, IDE_LOWCYL );
    IoOutByte( (sector & 0x00ff0000) >> 16, IDE_HIGHCYL );
    IoOutByte( ((sector & 0x0f000000) >> 24) | 0xe0, IDE_SELECT );
    IoOutByte( 0x30, IDE_COMMAND );

    for( i = 0 ; i < 256 ; i++ ) {
    
        IoOutWord( *buf | (*(buf + 1) << 8) , IDE_DATA );
        buf += 2;
    }
}



void IDEGetIdentify( void ) {

    IoOutByte( 0xec, IDE_COMMAND );
}



void IDEInit( void ) {

}



