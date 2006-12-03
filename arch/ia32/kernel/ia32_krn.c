/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * ia32_krn.c -- OluxOS IA32 kernel entry point
 *
 */


void ia32_krn_entry( void ) {


    ia32_MmPageInit();
    ia32_TcClear();
    ia32_TcPrint( "Copyright (C) 2006 Olux Organization all rights reserved.\n" );
    ia32_TcPrint( "Welcome to OluxOS v0.1\n" );


    for( ; ; );


}
