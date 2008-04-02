/*
 * Copyright (C) 2006 -  2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * sercon.c -- OluxOS IA32 Serial Console Routines
 *
 */
#include <types.h>
#include <clib.h>
#include <ia32/io.h>
#include <driver/console.h>
#include <driver/serial.h>
#include <driver/sercon.h>


static s8 buf[ CONSOLE_BUF_LEN ];


//
// ScPrint      -- Print string on serial console
//
// Input:
//  format      -- String to print
//  ...         -- Arguments
//
// Return:
//  None
//
void ScPrint( const s8 *format, ... ) {

    s8 *p;


    // Handle String Format
    if( CbFmtPrint( buf, CONSOLE_BUF_LEN, format, (&format) + 1 ) ) {
    
        return;
    }

    
    // Output to console
    for( p = buf ; *p ; p++ ) {
    
        // Output to VGA Text Mode Screen
        SrPutChar( *p );
    }
}


