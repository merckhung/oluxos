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
#include <termios.h>

#include <otypes.h>
#include <packet.h>

#include <ncurses.h>
#include <panel.h>

#include <kdbger.h>


static void help( void ) {

    fprintf( stderr, "\nCopyright (c) 2011 Olux Organization All rights reserved.\n\n" );
    fprintf( stderr, "OluxOS Kernel Debugger, Version "KDBGER_VERSION"\n" );
    fprintf( stderr, "Author: Merck Hung <merckhung@gmail.com>\n" );
    fprintf( stderr, "help: kdbger [-d /dev/ttyS0] [-h]\n\n" );
	fprintf( stderr, "\t-d\tUART TTY device, default is " KDBGER_DEF_TTYDEV "\n");
    fprintf( stderr, "\t-h\tPrint help and exit\n\n");
}


static s32 configureTtyDevice( s32 fd ) {

	struct termios termSetting;

	if( tcgetattr( fd, &termSetting ) )
		return 1;

	if( cfsetspeed( &termSetting, B115200 ) )
		return 1;

    if( tcsetattr( fd, TCSANOW, &termSetting ) )
        return 1;

	return 0;
}


void initColorPairs( void ) {

    init_pair( WHITE_RED, COLOR_WHITE, COLOR_RED );
    init_pair( WHITE_BLUE, COLOR_WHITE, COLOR_BLUE );
    init_pair( BLACK_WHITE, COLOR_BLACK, COLOR_WHITE );
    init_pair( CYAN_BLUE, COLOR_CYAN, COLOR_BLUE );
    init_pair( RED_BLUE, COLOR_RED, COLOR_BLUE );
    init_pair( YELLOW_BLUE, COLOR_YELLOW, COLOR_BLUE );
    init_pair( BLACK_GREEN, COLOR_BLACK, COLOR_GREEN );
    init_pair( BLACK_YELLOW, COLOR_BLACK, COLOR_YELLOW );
    init_pair( YELLOW_RED, COLOR_YELLOW, COLOR_RED );
    init_pair( YELLOW_BLACK, COLOR_YELLOW, COLOR_BLACK );
    init_pair( WHITE_YELLOW, COLOR_WHITE, COLOR_YELLOW );
	init_pair( RED_WHITE, COLOR_RED, COLOR_WHITE );
}


void updateStatusTimer( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	time_t timer;
	struct tm *nowtime;
	u32 thisSecond;
	u8 update = 0;
	s8 stsBuf[ KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR ];

	// Read second
	thisSecond = time( &timer );
	if( thisSecond == -1 )
		return;

	// Convert to localtime
	nowtime = localtime( &timer );
	if( !nowtime )
		return;

	if( pKdbgerUiProperty->lastSecond < thisSecond ) {

		// Update timer per second
		printWindowAt( pKdbgerUiProperty->kdbgerBasePanel, 
			time,
			KDBGER_STRING_NLINE,
			KDBGER_MAX_TIMESTR,
			KDBGER_MAX_LINE,
			KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR,
			BLACK_WHITE,
			"%2.2d:%2.2d:%2.2d",
			nowtime->tm_hour,
			nowtime->tm_min,
			nowtime->tm_sec );

		update = 1;
	}

	if( pKdbgerUiProperty->statusStr && ((thisSecond - pKdbgerUiProperty->lastSecond) >= KDBGER_STS_INTV_SECS) ) {

		s32 len = strlen( pKdbgerUiProperty->statusStr );
		if( pKdbgerUiProperty->strIdx >= len )
			pKdbgerUiProperty->strIdx = 0;

		if( (len - pKdbgerUiProperty->strIdx) > (KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR) )
			len = KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR;

		snprintf( stsBuf, len, "%s", (pKdbgerUiProperty->statusStr + pKdbgerUiProperty->strIdx) );

		// Update status bar
    	printWindow( 
			pKdbgerUiProperty->kdbgerBasePanel,
			status,
			KDBGER_STRING_NLINE,
			KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR,
			KDBGER_MAX_LINE,
			KDBGER_MIN_COLUMN,
			RED_WHITE,
			"%s",
			stsBuf );

		pKdbgerUiProperty->strIdx++;
		update = 1;
	}

	if( update )
		pKdbgerUiProperty->lastSecond = thisSecond;
}


void printBasePlane( kdbgerUiProperty_t *pKdbgerUiProperty ) {

    // Background Color
    printWindow( 
		pKdbgerUiProperty->kdbgerBasePanel, 
		background,
		KDBGER_MAX_LINE,
		KDBGER_MAX_COLUMN,
		KDBGER_MIN_LINE,
		KDBGER_MIN_COLUMN,
		WHITE_BLUE,
		"" );

    // Logo
    printWindow(
		pKdbgerUiProperty->kdbgerBasePanel,
		logo,
		KDBGER_STRING_NLINE,
		KDBGER_MAX_COLUMN,
		KDBGER_MIN_LINE,
		KDBGER_MIN_COLUMN,
		WHITE_RED,
		KDBGER_PROGRAM_FNAME );

	// Copyright
    printWindow(
		pKdbgerUiProperty->kdbgerBasePanel,
		copyright,
		KDBGER_STRING_NLINE,
		strlen( KDBGER_AUTHER_NAME ),
		KDBGER_MIN_LINE,
		KDBGER_MAX_COLUMN - strlen( KDBGER_AUTHER_NAME ),
		WHITE_RED, KDBGER_AUTHER_NAME );

	// Update screen
    update_panels();
    doupdate();
}


s32 main( s32 argc, s8 **argv ) {

	s8 c, ttyDevice[ KDBGER_MAX_PATH ];
	kdbgerUiProperty_t kdbgerUiProperty =
	{ 0, 0, KHF_INIT, KHF_INIT, { 0 }, NULL,
		{ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
		{ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
	 0, NULL, NULL, 0, 0, 0, NULL, 0, NULL, 0 };


	// Initialization
	strncpy( ttyDevice, KDBGER_DEF_TTYDEV, KDBGER_MAX_PATH );
	kdbgerUiProperty.pKdbgerCommPkt = (kdbgerCommPkt_t *)kdbgerUiProperty.pktBuf;
	kdbgerUiProperty.statusStr = KDBGER_HELP_TXT;


	// Handle parameters
    while( (c = getopt( argc, argv, "d:h" )) != EOF ) {

        switch( c ) {

			case 'd':

				strncpy( ttyDevice, optarg, KDBGER_MAX_PATH );
				break;

            case 'h':

                help();
                return 0;

            default:

                help();
                return 1;
        }
    }


	// Open TTY device
	kdbgerUiProperty.fd = open( ttyDevice, O_RDWR );
	if( kdbgerUiProperty.fd < 0 ) {

		fprintf( stderr, "Cannot open device %s\n", ttyDevice );
		return 1;
	}


	// Configure TTY device
	if( configureTtyDevice( kdbgerUiProperty.fd ) ) {

		fprintf( stderr, "Cannot configure device %s\n", ttyDevice );
		goto ErrExit;
	}


	// Connect to OluxOS Kernel
	if( connectToOluxOSKernel( &kdbgerUiProperty ) ) {

		fprintf( stderr, "Cannot connect to OluxOS Kernel via %s\n", ttyDevice );
		goto ErrExit;
	}

	// Read PCI list
	if( readPciList( &kdbgerUiProperty ) ) {

		fprintf( stderr, "Cannot get PCI device listing\n" );
		goto ErrExit;
	}

	// Read E820 list
	if( readE820List( &kdbgerUiProperty ) ) {

		fprintf( stderr, "Cannot get E820 listing\n" );
		goto ErrExit1;
	}


	// Initialize ncurses
    initscr();
    start_color();
    cbreak();
    noecho();
    nodelay( stdscr, 1 );
    keypad( stdscr, 1 );
    curs_set( 0 );
	initColorPairs();


	// Print base panel
	printBasePlane( &kdbgerUiProperty );


	// Main loop
	for( ; ; ) {


		// Get keyboard input
		kdbgerUiProperty.inputBuf = getch();
		switch( kdbgerUiProperty.inputBuf ) {

			// ESC
			case KBPRS_ESC:
				goto Exit;

			// Help
			case KBPRS_F1:
				break;

			// PCI/PCI-E listing
			case KBPRS_U_L:
			case KBPRS_L_L:
				kdbgerUiProperty.kdbgerPreviousHwFunc = kdbgerUiProperty.kdbgerHwFunc;
				kdbgerUiProperty.kdbgerHwFunc = KHF_PCIL;
				break;

			// PCI/PCI-E config space
			case KBPRS_U_P:
			case KBPRS_L_P:
				kdbgerUiProperty.kdbgerPreviousHwFunc = kdbgerUiProperty.kdbgerHwFunc;
				kdbgerUiProperty.kdbgerHwFunc = KHF_PCI;
				break;

			// I/O
			case KBPRS_U_I:
			case KBPRS_L_I:
				kdbgerUiProperty.kdbgerPreviousHwFunc = kdbgerUiProperty.kdbgerHwFunc;
				kdbgerUiProperty.kdbgerHwFunc = KHF_IO;
				break;

			// Memory
			case KBPRS_U_M:
			case KBPRS_L_M:
				kdbgerUiProperty.kdbgerPreviousHwFunc = kdbgerUiProperty.kdbgerHwFunc;
				kdbgerUiProperty.kdbgerHwFunc = KHF_MEM;
				break;

			default:
				kdbgerUiProperty.kdbgerPreviousHwFunc = kdbgerUiProperty.kdbgerHwFunc;
				if( kdbgerUiProperty.kdbgerHwFunc == KHF_INIT )
					kdbgerUiProperty.kdbgerHwFunc = KHF_MEM; // Default
				break;
		}


		// Clear previous function
		if( kdbgerUiProperty.kdbgerPreviousHwFunc 
			!= kdbgerUiProperty.kdbgerHwFunc ) {

			// Clear screen & reset
			clearDumpBasePanel( &kdbgerUiProperty );
			clearDumpUpdatePanel( &kdbgerUiProperty );
			kdbgerUiProperty.dumpByteBase = 0;
			kdbgerUiProperty.dumpByteOffset = 0;

			// Print
			switch( kdbgerUiProperty.kdbgerHwFunc ) {

				default:
				case KHF_MEM:
					kdbgerUiProperty.infoStr = KDBGER_INFO_MEMORY_BASE;
					printDumpBasePanel( &kdbgerUiProperty );
					break;

				case KHF_IO:
					kdbgerUiProperty.infoStr = KDBGER_INFO_IO_BASE;
					printDumpBasePanel( &kdbgerUiProperty );
					break;

				case KHF_PCI:
					break;

				case KHF_PCIL:
					break;
			}
		}


		switch( kdbgerUiProperty.kdbgerHwFunc ) {

			default:
			case KHF_MEM:
				handleKeyPressForDumpPanel( &kdbgerUiProperty );
				if( !readMemory( &kdbgerUiProperty ) )
					printDumpUpdatePanel( &kdbgerUiProperty );
				break;

			case KHF_IO:
				handleKeyPressForDumpPanel( &kdbgerUiProperty );
				if( !readIo( &kdbgerUiProperty ) )
					printDumpUpdatePanel( &kdbgerUiProperty );
				break;

			case KHF_PCI:
				break;

			case KHF_PCIL:
				break;
		}


        // Update timer
		updateStatusTimer( &kdbgerUiProperty );

		// Refresh Screen
		update_panels();
		doupdate();

		// Delay for a while
		usleep( 30000 );
	}

Exit:

	// Terminate ncurses
	endwin();


	// Free resources
	free( kdbgerUiProperty.pKdbgerE820record );

ErrExit1:
	free( kdbgerUiProperty.pKdbgerPciDev );

ErrExit:
	// Close TTY device
	close( kdbgerUiProperty.fd );

    return 0;
}



