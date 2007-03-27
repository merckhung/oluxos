/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * fat.c -- OluxOS FAT file system driver
 *
 */
#include <types.h>
#include <clib.h>
#include <ia32/debug.h>
#include <driver/ide.h>
#include <fs/fat.h>


void FsFatInit( void ) {

    __s8 buf[ 512 ];
    __s32 i;

    ReadSector( 0 );
    ReadData( buf );

    
    for( i = 0 ; i < 512 ; i++ ) {

        if( !( i % 26 ) ) {
        
            DbgPrint( "\n" );
        }

        DbgPrint( "%2X ", *(buf + i) );
    }
}


