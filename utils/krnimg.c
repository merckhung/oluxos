/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * krnimg.c - OluxOS Kernel Image Generator
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#define VERSION "krnimg Version 0.1 (c) 2006 - 2007 Olux Organization All rights reserved."


#define DEFKRN  "kernel"
#define DEFBOOT "bootsect"
#define DEFIMG  "OluxOS.krn"
#define BLKSZ   4096
#define HDDBLK  512
#define LENNAME 80
#define OFFBLK  0x1fd



static void usage( void ) {

    fprintf( stderr, "\n" );
    fprintf( stderr, VERSION"\n" );
    fprintf( stderr, "Usage: krnimg -b " DEFBOOT " -k " DEFKRN " -o " DEFIMG " [-h]\n\n" );
    fprintf( stderr, "\t-b\tOluxOS Boot Sector(0x00007c00) Binary\n" );
    fprintf( stderr, "\t-k\tOluxOS Kernel Main(0x00100000) Binary\n" );
    fprintf( stderr, "\t-o\tOluxOS Kernel Image(Output)\n" );
    fprintf( stderr, "\t-h\tprint this message.\n");
    fprintf( stderr, "\n");
}


int main( int argc, char **argv ) {

    int kfd, bfd, ifd;
    char krn_name[ LENNAME ], boot_name[ LENNAME ], img_name[ LENNAME ];
    int krn_sz, boot_sz;
    int set = 0, err = 1;
    char buf[ BLKSZ ];
    int i;
    char c;

    extern char *optarg;
    extern int optind;


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
    // Create Kenrel Image
    //
    ifd = open( img_name, O_RDWR | O_CREAT, 0644 );
    if( ifd < 0 ) {
    
        fprintf( stderr, "Cannot create file of kernel image %s\n", img_name );
        goto close_kfd;
    }


    //
    // Write Boot Sector
    //
    for( boot_sz = 0 ; ; boot_sz += i ) {
    
        i = read( bfd, buf, BLKSZ );
        if( !i ) {
        
            break;
        }

        if( i != write( ifd, buf, i ) ) {
        
            fprintf( stderr, "Cannot Write Boot Sector\n" );
            goto close_ifd;
        }
    }


    //
    // Write Main Kernel
    //
    for( krn_sz = 0 ; ; krn_sz += i ) {
    
        i = read( kfd, buf, BLKSZ );
        if( !i ) {
        
            break;
        }
    
        if( i != write( ifd, buf, i ) ) {

            fprintf( stderr, "Cannot Write Main Kernel\n" );
            goto close_ifd;
        }
    }


    //
    // Write Block Number
    //
    if( lseek( ifd, OFFBLK, SEEK_SET ) < 0 ) {
    
        fprintf( stderr, "Cannot go to Block Variable at offset %d\n", OFFBLK );
        goto close_ifd;
    }


    c = (boot_sz + krn_sz + HDDBLK) / HDDBLK;
    if( write( ifd, &c, 1 ) != 1 ) {
    
        fprintf( stderr, "Cannot write new Block Variable\n" );
        goto close_ifd;
    }
    


    //
    // Write Done
    //
    printf( "Created Kernel Image %s successfully\n", img_name );
    printf( "BootSector: %d bytes\n", boot_sz );
    printf( "Main Kernel: %d bytes (Start from: 0x%8.8X)\n", krn_sz, boot_sz );
    printf( "Kernel Image Size: %d bytes, %d Blocks\n", boot_sz + krn_sz, c );


    err = 0;

close_ifd:
    close( ifd );

close_kfd:
    close( kfd );

close_bfd:
    close( bfd );

open_err:

    return err;
}


