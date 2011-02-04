/*
 * Copyright (C) 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: kdbger.c
 * Description:
 *	OluxOS Kernel Debugger
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include <otypes.h>
#include <olux.h>


static s8 *ErrorStr     = "\nError: ";
static s8 *ErrInvParm   = "Invalid Parameters";
static s8 *ErrCantOpen  = "Cannot Open Device";


static s8 Krnname[ OLUX_MAX_PATHNAME ];
static s8 Drivename[ OLUX_MAX_PATHNAME ];
static s8 OldLdr[ OLUX_SZ_SECTOR ];
static s8 Verbose = 0;
static s8 Replace = 1;



static void Usage( void ) {

    fprintf( stderr, "\nCopyright (c) 2011 Olux Organization All rights reserved.\n\n" );
    fprintf( stderr, "OluxOS Kernel Debugger, Version "OLUX_VERSION"\n\n" );

    fprintf( stderr, "Authors: Merck Hung <merck@olux.org>\n\n" );

    fprintf( stderr, "Usage: kdbger \n\n" );
    fprintf( stderr, "\t-h\tPrint help and exit\n");
	fprintf( stderr, "\t-v\tVerbose more message\n");
}



s32 main( s32 argc, s8 **argv ) {

	u32 func, c, RldrAddr;
	s32 fd, hfd, krnsz, oldrsecno, oldroff, krnsecs, rwbytes;
	s8 *krnp;



    ////////////////////////////////////////////////////////////////////////////
    // Initialization                                                         //
    ////////////////////////////////////////////////////////////////////////////
	memset( Krnname, 0, OLUX_MAX_PATHNAME );
	memset( Drivename, 0, OLUX_MAX_PATHNAME );
	memset( OldLdr, 0, OLUX_SZ_SECTOR );
	RldrAddr = 0;
	func = 0;



    ////////////////////////////////////////////////////////////////////////////
    // Handle Paramaters                                                      //
    ////////////////////////////////////////////////////////////////////////////
    while( (c = getopt( argc, argv, "vh" )) != EOF ) {

        switch( c ) {

			case 'v':

				Verbose = 1;
				break;

            case 'h':

                Usage();
                return 0;

            default:

                fprintf( stderr, "%s%s\n", ErrorStr, ErrInvParm );
                Usage();
                return 1;
        }
    }


    ////////////////////////////////////////////////////////////////////////////
    // Release resource                                                       //
    ////////////////////////////////////////////////////////////////////////////


    return 0;
}



