/*
 * Copyright (C) 2006 -  2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * menu.c -- OluxOS IA32 text mode console Screen Setup Menu routines
 *
 */
#include <types.h>
#include <clib.h>
#include <ia32/io.h>
#include <ia32/debug.h>
#include <driver/console.h>


static volatile __u8 *ScnPtr = (__u8 *)0xb8000;


void MenuBackground( void ) {

    __s32 i;

    for( i = 0 ; i < COLUMN * LINE * 2 ; i++ ) {
    
        if( i % 2 ) {
        
            *(ScnPtr + i) = 0x30;
        }
        else {
        
            *(ScnPtr + i) = ' ';
        }
    }
}


void MenuTitle( void ) {

    __s8 *title = "OluxOS Setup Utility";
    __s32 i;

    for( i = 0 ; i < strlen( title ) ; i++ ) {
    
        *(ScnPtr + (i * 2) + (30 * 2)) = *(title + i);
    }
}


void MenuItem( void ) {

    __s8 *item = "     Main     Advanced     Security     Power     Boot     Exit                 ";
    __s32 i, j, k;

    i = COLUMN * 2 * 1;
    j = strlen( item ) * 2 + i;

    for( k = 0 ; i < j ; i++ ) {
    
        if( i % 2 ) {
        
            *(ScnPtr + i) = 0x17;
        }
        else {
        
            *(ScnPtr + i) = *(item + k);
            k++;
        }
    }
}


void MenuHelp( void ) {

    __s8 *help = "  F1   Help   ||  Select Item   -/+    Change Values       F9   Setup Defaults  " \
                 "  Esc  Exit   <>  Select Menu   Enter  Select > Sub-Menu   F10  Save and Exit   ";
    __s32 i, j, k;

    i = COLUMN * 2 * 23;
    j = strlen( help ) * 2 + i;

    for( k = 0 ; i < j ; i++ ) {
    
        if( !(i % 2) ) {
        
            *(ScnPtr + i) = *(help + k);
            k++;
        }
    }
}


void MenuCentral( void ) {

    __s32 i = COLUMN  * 2;
    __s32 j = COLUMN  * 23;

    for( ; i < j ; i++ ) {
    
        *(ScnPtr + (i * 2) + 1) = 0x70;
    }
}


void MenuInit( void ) {


    MenuBackground();
    MenuTitle();
    MenuItem();
    MenuCentral();
    MenuHelp();
}


