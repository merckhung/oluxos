/*
 * Copyright (C) 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: instkrn.c
 * Description:
 *	OluxOS Kernel Image Installer
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

    fprintf( stderr, "\nCopyright (c) 2008 Olux Organization All rights reserved.\n\n" );
    fprintf( stderr, "OluxOS Kernel Image Installer, Version "OLUX_VERSION"\n\n" );

    fprintf( stderr, "Authors: Merck Hung <merck@olux.org>\n\n" );

    fprintf( stderr, "Usage: instkrn -k <KERNEL_NAME> -d <HDD_NAME> ( [-a <ADDR> ] | -[hvr] )\n\n" );
    fprintf( stderr, "\t-h\tPrint help and exit\n");
	fprintf( stderr, "\t-v\tVerbose more message\n");
    fprintf( stderr, "\t-k\tKernel Krnname\n");
	fprintf( stderr, "\t-d\tHard Disk Drive\n");
	fprintf( stderr, "\t-a\tSpecify a address for original bootloader, ex: 0x7A00\n");
	fprintf( stderr, "\t-r\tReplace kernel image only, don't relocate bootloader again\n\n");
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
    while( (c = getopt( argc, argv, "k:d:a:rvh" )) != EOF ) {

        switch( c ) {


            case 'k':

                strncpy( Krnname, optarg, OLUX_MAX_PATHNAME );
                break;


			case 'd':

				strncpy( Drivename, optarg, OLUX_MAX_PATHNAME );
				break;


			case 'a':

				// Convert ASCII to Binary
				if( ParseOneParameter( optarg, &RldrAddr ) == FALSE ) {

					fprintf( stderr, "Invalid format of address\n" );
					return 1;
				}
				if( RldrAddr % OLUX_SZ_SECTOR ) {

					fprintf( stderr, "Please align sector size: %d\n", OLUX_SZ_SECTOR );
					return 1;
				}
				break;


			case 'r':

				Replace = 0;
				break;


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


    // Check kernel filename
    if( !(*Krnname) ) {
    
        fprintf( stderr, "\nPlease specify a kernel filename\n" );
        Usage();
        return 1;
    }


	// Check Drive name
	if( !(*Drivename) ) {

		fprintf( stderr, "\nPlease specify a HDD drive name\n" );
		Usage();
		return 1;
	}



    ////////////////////////////////////////////////////////////////////////////
    // Manipulation                                                           //
    ////////////////////////////////////////////////////////////////////////////	
	


	// Open kernel image
	fd = open( Krnname, O_RDONLY );
	if( fd < 0 ) {
	
		fprintf( stderr, "%s%s %s\n", ErrorStr, ErrCantOpen, Krnname );
		return 1;
	}


	// Open Hard Disk
	hfd = open( Drivename, O_RDWR );
	if( hfd < 0 ) {

		fprintf( stderr, "Cannot open hard disk drive\n" );
		goto ErrExit;
	}


	// Get kernel size
	krnsz = lseek( fd, 0, SEEK_END );
	if( krnsz < 1 ) {

		fprintf( stderr, "The size of kernel image < 1\n" );
		goto ErrExit;
	}


	// Allocate buffer for kernel image
	krnp = (s8 *)malloc( krnsz );
	if( !krnp ) {

		fprintf( stderr, "Cannot allocate memory for kernel image\n" );
		goto ErrExit;
	}


	// Read kernel image to buffer
	lseek( fd, 0, SEEK_SET );
	rwbytes = read( fd, krnp, krnsz );
	if( krnsz != rwbytes ) {

		fprintf( stderr, "Error occurred while reading kernel image (%d != %d)\n", krnsz, rwbytes );
		goto ErrExit1;
	}


	// Read original bootloader
	lseek( hfd, 0, SEEK_SET );
	rwbytes = read( hfd, OldLdr, OLUX_SZ_SECTOR );
	if( OLUX_SZ_SECTOR != rwbytes ) {

		fprintf( stderr, "Error occurred while reading original bootloader (%d != %d)\n", OLUX_SZ_SECTOR, rwbytes );
		goto ErrExit1;
	}


	// Calculate Kernel information
	if( krnsz % OLUX_SZ_SECTOR ) {

		krnsecs = krnsz / OLUX_SZ_SECTOR + 1;
	}
	else {

		krnsecs = krnsz / OLUX_SZ_SECTOR;
	}


	// Relate original bootloader to
	oldrsecno = krnsecs;
	oldroff = oldrsecno * OLUX_SZ_SECTOR;
	if( RldrAddr > oldroff ) {

		oldrsecno = RldrAddr / OLUX_SZ_SECTOR;
		oldroff = RldrAddr;
	}
	
	
	// Show information
	if( Verbose ) {

		printf( "Kernel size        : 0x%8.8X (%d)\n", krnsz, krnsz );
		printf( "Kernel sectors     : 0x%8.8X (%d)\n", krnsecs, krnsecs );
		printf( "Original ldr sector: 0x%8.8X (%d)\n", oldrsecno, oldrsecno );
		printf( "Original ldr offset: 0x%8.8X (%d)\n", oldroff, oldroff );
	}


	// Relocate bootloader
	if( Replace ) {

		lseek( hfd, oldroff, SEEK_SET );
		rwbytes = write( hfd, OldLdr, OLUX_SZ_SECTOR );
		if( rwbytes != OLUX_SZ_SECTOR ) {

			fprintf( stderr, "Error occurred while relocating original bootloader\n" );
			goto ErrExit1;
		}


		printf( "Original bootloader relocated\n" );
	}


	// Copy partition tables and disk signature
	memcpy( (krnp + 0x1BE), (OldLdr + 0x1BE) , 16 * 4 );
	memcpy( (krnp + 0x1B8), (OldLdr + 0x1B8) , 4 );


	// Install kernel image
	lseek( hfd, 0, SEEK_SET );
	rwbytes = write( hfd, krnp, krnsz );
	if( rwbytes != krnsz ) {

		fprintf( stderr, "Error occurred while installing kernel image\n" );
		goto ErrExit1;
	}



	// Successful message
	printf( "Kernel image installed\n" );



    ////////////////////////////////////////////////////////////////////////////
    // Release resource                                                       //
    ////////////////////////////////////////////////////////////////////////////


ErrExit1:

	// Free kernel image buffer
	free( krnp );

ErrExit:

	// Close file
	close( fd );


    return 0;
}



