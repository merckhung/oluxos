/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: krnimg.c
 * Description:
 * 	OluxOS Kernel Image Generator
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <version.h>

#include <otypes.h>
#include <olux.h>


#define VERSION 			"krnimg version 0.1 (C) "COPYRIGHT_YEAR" "COMPANY_NAME
#define DEFKRN  			"kernel"
#define DEFBOOT 			"bootsect"
#define DEFIMG  			"OluxOS.krn"
#define HDDBLK  			512
#define LENNAME 			80
#define KRN_IMG_NR_SECS		2
#define	KRN_CODE_NR_SECS	3



static void usage( void ) {

    fprintf( stderr, "\n" );
    fprintf( stderr, VERSION"\n" );
    fprintf( stderr, "Usage: krnimg -b " DEFBOOT " -k " DEFKRN " -o " DEFIMG " [-h]\n\n" );
    fprintf( stderr, "\t-b\t"KRN_NAME" Binary of Boot Sector\t(0x0007c000)\n" );
    fprintf( stderr, "\t-k\t"KRN_NAME" Binary of Kernel Code\t(0x00100000)\n" );
    fprintf( stderr, "\t-o\t"KRN_NAME" Kernel Image\t\t(Output)\n" );
    fprintf( stderr, "\t-h\tprint this message.\n");
    fprintf( stderr, "\n");
}



u32 CalNrSectors( u32 bufsz ) {

	if( bufsz % HDDBLK ) {

		return (bufsz / HDDBLK) + 1;
	}
	else {

		return bufsz / HDDBLK;
	}
}



s32 main( s32 argc, s8 **argv ) {

    s32 kfd, bfd, ifd;
    s8 krn_name[ LENNAME ], boot_name[ LENNAME ], img_name[ LENNAME ];
    s32 krn_sz, boot_sz;
    s32 set = 0, err = 1;
    s32 i;
    s8 c, *kbuf, *bbuf;


    //
    // Apply default values
    //
    strncpy( krn_name, DEFKRN, LENNAME );
    strncpy( boot_name, DEFBOOT, LENNAME );
    strncpy( img_name, DEFIMG, LENNAME );


    while( (c = getopt( argc, argv, "o:b:k:h" )) != EOF ) {

        switch( c ) {


            case 'b' :
                strncpy( boot_name, optarg, LENNAME );
                set |= 0x01;
                break;


            case 'k' :
                strncpy( krn_name, optarg, LENNAME );
                set |= 0x02;
                break;

            
            case 'o' :
                strncpy( img_name, optarg, LENNAME );
                set |= 0x04;
                break;


            case 'h' :
            default :
                usage();
                return 1;
        }
    }    



	if( set != 0x07 ) {

		usage();
		return 1;
	}



    //
    // Open bootsector binary
    //
    bfd = open( boot_name, O_RDONLY );
    if( bfd < 0 ) {

        fprintf( stderr, "Cannot open file of bootsector binary: %s\n", boot_name );
        goto open_err;
    }



    //
    // Open kernel binary
    //
    kfd = open( krn_name, O_RDONLY );
    if( kfd < 0 ) {

        fprintf( stderr, "Cannot open file of kernel binary: %s\n", krn_name );
        goto close_bfd;
    }


	//
	// Get image sizes
	//
	boot_sz = lseek( bfd, 0, SEEK_END );	
	krn_sz = lseek( kfd, 0, SEEK_END );
	if( (boot_sz < 1) || (krn_sz < 1) ) {

		fprintf( stderr, "Kernel or Boot code size < 1\n" );
		goto close_kfd;
	}
	lseek( bfd, 0, SEEK_SET );
	lseek( kfd, 0, SEEK_SET );


	// Align the size of boot code
	if( boot_sz % 16 ) {

		boot_sz = ((boot_sz + 16) / 16) * 16;
	}


	// Allocate memory
	bbuf = malloc( boot_sz );
	if( !bbuf ) {

		fprintf( stderr, "Cannot allocate memory for the buffer of boot code\n" );
		goto close_kfd;
	}
	memset( bbuf, 0, boot_sz );


	kbuf = malloc( krn_sz );
	if( !kbuf ) {

		fprintf( stderr, "Cannot allocate memory for the buffer of kernel code\n" );
		goto free_bbuf;
	}
	memset( kbuf, 0, krn_sz );



    //
    // Create Kenrel Image
    //
    ifd = open( img_name, O_RDWR | O_CREAT, 0644 );
    if( ifd < 0 ) {
    
        fprintf( stderr, "Cannot create file of kernel image %s\n", img_name );
        goto free_kbuf;
    }



    //
    // Write Boot code
    //
	i = read( bfd, bbuf, boot_sz );
	if( i < 1 ) {

		fprintf( stderr, "Error occurred while reading boot code\n" );
		goto close_ifd;
	}
	i = write( ifd, bbuf, boot_sz );
	if( i != boot_sz ) {

		fprintf( stderr, "Error occurred while writing boot code\n" );
		goto close_ifd;
	}



    //
    // Write Main Kernel
    //
	i = read( kfd, kbuf, krn_sz );
	if( i < 1 ) {

		fprintf( stderr, "Error occurred while reading kernel code\n" );
		goto close_ifd;
	}
	i = write( ifd, kbuf, krn_sz );
	if( i != krn_sz ) {

		fprintf( stderr, "Error occurred while writing kernel code\n" );
		goto close_ifd;
	}


    //
    // Write the number of sectors of kernel image
    //
    if( lseek( ifd, KRN_IMG_NR_SECS, SEEK_SET ) != KRN_IMG_NR_SECS ) {
    
        fprintf( stderr, "Cannot go to offset %d\n", KRN_IMG_NR_SECS );
        goto close_ifd;
    }
	c = CalNrSectors( boot_sz + krn_sz );
    if( write( ifd, &c, 1 ) != 1 ) {
    
        fprintf( stderr, "Cannot write the number of sectors of kernel image\n" );
        goto close_ifd;
    }


    //
    // Write the number of sectors of kernel code
    //
    if( lseek( ifd, KRN_CODE_NR_SECS, SEEK_SET ) != KRN_CODE_NR_SECS ) {
    
        fprintf( stderr, "Cannot go to offset %d\n", KRN_CODE_NR_SECS );
        goto close_ifd;
    }
	c = CalNrSectors( krn_sz );
    if( write( ifd, &c, 1 ) != 1 ) {
    
        fprintf( stderr, "Cannot write the number of sectors of kernel code\n" );
        goto close_ifd;
    }


    //
    // Write Done
    //
    printf( "Created Kernel Image %s successfully\n", img_name );
    printf( "Boot Code Size    : 0x%8.8X (%6d) bytes, 0x%2.2X (%4d) sectors\n", boot_sz, boot_sz, CalNrSectors( boot_sz ), CalNrSectors( boot_sz ) );
    printf( "Kernel Code Size  : 0x%8.8X (%6d) bytes, 0x%2.2X (%4d) sectors\n", krn_sz, krn_sz, CalNrSectors( krn_sz ), CalNrSectors( krn_sz ) );
    printf( "Kernel Image Size : 0x%8.8X (%6d) bytes, 0x%2.2X (%4d) sectors\n", boot_sz + krn_sz, boot_sz + krn_sz, c, c );



    err = 0;


close_ifd:
	close( ifd );

free_kbuf:
	free( kbuf);

free_bbuf:
	free( bbuf );

close_kfd:
    close( kfd );

close_bfd:
    close( bfd );

open_err:

    return err;
}


