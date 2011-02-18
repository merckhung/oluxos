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
	init_pair( WHITE_YELLOW, COLOR_WHITE, COLOR_YELLOW );
	init_pair( WHITE_BLACK, COLOR_WHITE, COLOR_BLACK );

    init_pair( BLACK_WHITE, COLOR_BLACK, COLOR_WHITE );
	init_pair( BLACK_GREEN, COLOR_BLACK, COLOR_GREEN );
	init_pair( BLACK_YELLOW, COLOR_BLACK, COLOR_YELLOW );

    init_pair( CYAN_BLUE, COLOR_CYAN, COLOR_BLUE );
	init_pair( CYAN_WHITE, COLOR_CYAN, COLOR_WHITE );

    init_pair( RED_BLUE, COLOR_RED, COLOR_BLUE );
	init_pair( RED_WHITE, COLOR_RED, COLOR_WHITE );

    init_pair( YELLOW_BLUE, COLOR_YELLOW, COLOR_BLUE );
    init_pair( YELLOW_RED, COLOR_YELLOW, COLOR_RED );
    init_pair( YELLOW_BLACK, COLOR_YELLOW, COLOR_BLACK );
	init_pair( YELLOW_WHITE, COLOR_YELLOW, COLOR_WHITE );

	init_pair( MAGENTA_BLUE, COLOR_MAGENTA, COLOR_BLUE );

	init_pair( GREEN_BLUE, COLOR_GREEN, COLOR_BLUE );
	init_pair( GREEN_WHITE, COLOR_GREEN, COLOR_WHITE );
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

	if( pKdbgerUiProperty->kdbgerBasePanel.lastSecond < thisSecond ) {

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

	if( pKdbgerUiProperty->kdbgerBasePanel.statusStr && ((thisSecond - pKdbgerUiProperty->kdbgerBasePanel.lastSecond) >= KDBGER_STS_INTV_SECS) ) {

		s32 len = strlen( pKdbgerUiProperty->kdbgerBasePanel.statusStr );
		if( pKdbgerUiProperty->kdbgerBasePanel.strIdx >= len )
			pKdbgerUiProperty->kdbgerBasePanel.strIdx = 0;

		if( (len - pKdbgerUiProperty->kdbgerBasePanel.strIdx) > (KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR) )
			len = KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR;

		snprintf( stsBuf, len, "%s", (pKdbgerUiProperty->kdbgerBasePanel.statusStr + pKdbgerUiProperty->kdbgerBasePanel.strIdx) );

		// Update status bar
    	printWindow( 
			pKdbgerUiProperty->kdbgerBasePanel,
			status,
			KDBGER_STRING_NLINE,
			KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR,
			KDBGER_MAX_LINE,
			KDBGER_MIN_COLUMN,
			BLACK_WHITE,
			"%s",
			stsBuf );

		pKdbgerUiProperty->kdbgerBasePanel.strIdx++;
		update = 1;
	}

	if( update )
		pKdbgerUiProperty->kdbgerBasePanel.lastSecond = thisSecond;
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
		WHITE_RED,
		KDBGER_AUTHER_NAME );

	// Update screen
    update_panels();
    doupdate();
}


void printBaseHelp( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	s8 *helpTxt = "Copyright (C) 2011 OluxOS Kernel Debugger, all right reserved.\n\
Author: Merck Hung <merckhung@gmail.com>\n\n\
  <F1>:  Help\n\
  <F2>:  PCI/PCI-E device listing\n\
  <F3>:  PCI/PCI-E device configuration space\n\
  <F4>:  CPU I/O space (Intel x86 arch only)\n\
  <F5>:  CPU Memory space\n\
  <F6>:  IDE hard drive content (1st HDD)\n\
  <F7>:  CMOS content (Intel x86 arch only)\n\
  <F8>:  I2C space\n\
  <F9>:  TBD\n\
  <F10>: TBD\n\
  <F11>: TBD\n\
  <F12>: TBD\n\
";

	if( pKdbgerUiProperty->kdbgerBasePanel.toggleHelp ) {

		printWindowMove(
			pKdbgerUiProperty->kdbgerBasePanel, 
			help,
			(KDBGER_MAX_LINE - 4),
			(KDBGER_MAX_COLUMN - 4),
			KDBGER_HELP_X_POS,
			KDBGER_HELP_Y_POS,
			BLACK_WHITE,
			"%s",
			helpTxt );
	}
	else
		destroyWindow( pKdbgerUiProperty->kdbgerBasePanel, help );
}


s32 main( s32 argc, s8 **argv ) {

	s8 c, ttyDevice[ KDBGER_MAX_PATH ];
	kdbgerUiProperty_t kdbgerUiProperty;


	// Initialization
	strncpy( ttyDevice, KDBGER_DEF_TTYDEV, KDBGER_MAX_PATH );
	memset( &kdbgerUiProperty, 0, sizeof( kdbgerUiProperty_t ) );
	kdbgerUiProperty.kdbgerHwFunc = kdbgerUiProperty.kdbgerPreviousHwFunc = KHF_INIT;
	kdbgerUiProperty.pKdbgerCommPkt = (kdbgerCommPkt_t *)kdbgerUiProperty.pktBuf;
	kdbgerUiProperty.kdbgerBasePanel.statusStr = KDBGER_WELCOME_TXT;


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
//		if( kdbgerUiProperty.inputBuf != -1 )
//			printf( "C = 0x%8.8X\n", kdbgerUiProperty.inputBuf );
		switch( kdbgerUiProperty.inputBuf ) {

			// ESC
			case KBPRS_ESC:
				goto Exit;

			// Help
			case KBPRS_F1:
				kdbgerUiProperty.kdbgerBasePanel.toggleHelp = !kdbgerUiProperty.kdbgerBasePanel.toggleHelp;
				break;

			// PCI/PCI-E listing
			case KBPRS_F2:
				kdbgerUiProperty.kdbgerPreviousHwFunc = kdbgerUiProperty.kdbgerHwFunc;
				kdbgerUiProperty.kdbgerHwFunc = KHF_PCIL;
				break;

			// PCI/PCI-E config space
			case KBPRS_F3:
				kdbgerUiProperty.kdbgerPreviousHwFunc = kdbgerUiProperty.kdbgerHwFunc;
				kdbgerUiProperty.kdbgerHwFunc = KHF_PCI;
				break;

			// I/O
			case KBPRS_F4:
				kdbgerUiProperty.kdbgerPreviousHwFunc = kdbgerUiProperty.kdbgerHwFunc;
				kdbgerUiProperty.kdbgerHwFunc = KHF_IO;
				break;

			// Memory
			case KBPRS_F5:
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
			kdbgerUiProperty.kdbgerDumpPanel.byteBase = 0;
			kdbgerUiProperty.kdbgerDumpPanel.byteOffset = 0;

			// Print
			switch( kdbgerUiProperty.kdbgerHwFunc ) {

				default:
				case KHF_MEM:
					kdbgerUiProperty.kdbgerDumpPanel.infoStr = KDBGER_INFO_MEMORY_BASE;
					printDumpBasePanel( &kdbgerUiProperty );
					break;

				case KHF_IO:
					kdbgerUiProperty.kdbgerDumpPanel.infoStr = KDBGER_INFO_IO_BASE;
					printDumpBasePanel( &kdbgerUiProperty );
					break;

				case KHF_PCI:
					kdbgerUiProperty.kdbgerDumpPanel.infoStr = KDBGER_INFO_PCI_BASE;
					printDumpBasePanel( &kdbgerUiProperty );
					break;

				case KHF_PCIL:
					break;

				case KHF_IDE:
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
				handleKeyPressForDumpPanel( &kdbgerUiProperty );
				if( !readPci( &kdbgerUiProperty ) )
					printDumpUpdatePanel( &kdbgerUiProperty );
				break;

			case KHF_PCIL:
				break;

			case KHF_IDE:
				break;
		}

		// Help Text
		printBaseHelp( &kdbgerUiProperty );

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



