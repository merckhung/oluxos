/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * console.c -- OluxOS IA32 text mode console routines
 *
 */
#include "console.h"


static volatile unsigned char *video_mem = VIDEO_TEXT_ADDR;


void ia32_printf( void ) {

    char *hello = "Welcome to Olux Operating System!";

    int len = strlen( hello );
    int i, j;
    
    for( i = 0, j = 0 ; j < len  ; i += 2, j++ ) {
    
        *(video_mem + i) = hello[ j ];
    }
}


void ia32_cls( void ) {

    int len = 160 * 25;
    int i;

    for( i = 0 ; i < len ; i++ ) {
    
        *(video_mem + i) = ( i % 2 ) ? 0x07 : 0x00; 
    }
}



