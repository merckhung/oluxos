/*
 * Copyright (C) 2006 - 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: kdbger.h
 * Description:
 *  OluxOS Kernel Debugger header file
 *
 */


#define KDBGER_VERSION				"0.1"
#define KDBGER_MAX_PATH				256
#define KDBGER_DEF_PCIIDS			"/usr/share/misc/pci.ids"
#define KDBGER_DEF_TTYDEV			"/dev/pts/0"

#define KDBGER_MIN_LINE				0
#define KDBGER_MIN_COLUMN			0
#define KDBGER_MAX_LINE				23
#define KDBGER_MAX_COLUMN           80
#define KDBGER_MAX_READBUF			512

#define KDBGER_STRING_NLINE			1
#define KDBGER_MAX_TIMESTR			8
#define KDBGER_BYTE_PER_SCREEN		256

#define KDBGER_PROGRAM_FNAME		"OluxOS Kernel Debugger"
#define KDBGER_AUTHER_NAME			"Merck Hung <merckhung@gmail.com>"

#define KDBGER_INFO_BAR_FMT			"Type: "

#define KDBGER_INFO_MEMORY_FUNC		"Memory"
#define KDBGER_INFO_IO_FUNC			"I/O"
#define KDBGER_INFO_PCI_FUNC		"PCI/PCI-E"
#define KDBGER_INFO_PCIL_FUNC		"PCI/PCI-E Listing"
#define KDBGER_INFO_IDE_FUNC		"IDE"
#define KDBGER_INFO_CMOS_FUNC		"CMOS"

#define KDBGER_INFO_MEMORY_BASE		KDBGER_INFO_BAR_FMT KDBGER_INFO_MEMORY_FUNC "    Address: "
#define KDBGER_INFO_IO_BASE			KDBGER_INFO_BAR_FMT KDBGER_INFO_IO_FUNC "    Address: "
#define KDBGER_INFO_PCI_BASE		KDBGER_INFO_BAR_FMT KDBGER_INFO_PCI_FUNC "    "
#define KDBGER_INFO_IDE_BASE		KDBGER_INFO_BAR_FMT KDBGER_INFO_IDE_FUNC "    Address: "
#define KDBGER_INFO_CMOS_BASE		KDBGER_INFO_BAR_FMT KDBGER_INFO_CMOS_FUNC "    Address: "

#define KDBGER_INFO_MEMORY_BASE_FMT	"%8.8X-%8.8Xh"
#define KDBGER_INFO_IO_BASE_FMT		"%4.4Xh"
#define KDBGER_INFO_PCI_BASE_FMT	"Bus: %2.2Xh, Dev: %2.2Xh, Fun: %2.2Xh"
#define KDBGER_INFO_IDE_BASE_FMT	"%8.8X-%8.8Xh"
#define KDBGER_INFO_CMOS_BASE_FMT	"%2.2Xh"

#define KDBGER_FTITLE_PCI			"Vendor"
#define KDBGER_STITLE_PCI			"Device"

#define KDBGER_INFO_LINE			(KDBGER_MAX_LINE - 1)
#define KDBGER_INFO_COLUMN			0

#define KDBGER_HELP_LINE			(KDBGER_MAX_LINE - 4)
#define KDBGER_HELP_COLUMN			(KDBGER_MAX_COLUMN - 4)
#define KDBGER_HELP_X_POS			3
#define KDBGER_HELP_Y_POS			2

#define KDBGER_WELCOME_TXT			"Welcome to OluxOS Kernel Debugger. Copyright (C) 2006 - 2011, Merck Hung all right reserved. Contact Merck Hung, Email: merckhung@gmail.com, Cell: +886-988-682504, City: Taipei City, Taiwan."

#define KDBGER_DUMP_TOP_BAR			"00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F"
#define KDBGER_DUMP_LEFT_BAR		"000000100020003000400050006000700080009000A000B000C000D000E000F0"
#define KDBGER_DUMP_RTOP_BAR		"0123456789ABCDEF"

#define KDBGER_DUMP_OFF_LINE		4
#define KDBGER_DUMP_OFF_COLUMN		1
#define KDBGER_DUMP_HL_DIGITS		2
#define KDBGER_DUMP_HLA_DIGITS		1
#define KDBGER_DUMP_BITS_DIGITS		9

#define KDBGER_DUMP_BASEADDR_LINE	KDBGER_INFO_LINE
#define KDBGER_DUMP_FTITLE_LINE		1
#define KDBGER_DUMP_FTITLE_COLUMN	1
#define KDBGER_DUMP_STITLE_LINE		(KDBGER_DUMP_FTITLE_LINE + 1)
#define KDBGER_DUMP_STITLE_COLUMN   KDBGER_DUMP_FTITLE_COLUMN

#define KDBGER_DUMP_TOP_LINE		4
#define KDBGER_DUMP_TOP_COLUMN		6

#define KDBGER_DUMP_LEFT_LINE		(KDBGER_DUMP_TOP_LINE + 1)
#define KDBGER_DUMP_LEFT_COLUMN		1

#define KDBGER_DUMP_RTOP_LINE		KDBGER_DUMP_TOP_LINE
#define KDBGER_DUMP_RTOP_COLUMN		(KDBGER_DUMP_TOP_COLUMN + strlen(KDBGER_DUMP_TOP_BAR) + 2)

#define KDBGER_DUMP_BYTE_PER_LINE	16
#define KDBGER_DUMP_BUF_PER_LINE	(KDBGER_DUMP_BYTE_PER_LINE * 3 - 1)

#define KDBGER_DUMP_VALUE_LINE		(KDBGER_DUMP_TOP_LINE + 1)
#define KDBGER_DUMP_VALUE_COLUMN	(KDBGER_DUMP_LEFT_COLUMN + 5)
#define KDBGER_DUMP_ASCII_LINE		(KDBGER_DUMP_TOP_LINE + 1)
#define KDBGER_DUMP_ASCII_COLUMN	KDBGER_DUMP_RTOP_COLUMN
#define KDBGER_DUMP_ASCII_FILTER(x)	(((x) >= '!' && (x) <= '~')?(x):'.')

#define KDBGER_DUMP_VBUF_SZ			(KDBGER_BYTE_PER_SCREEN * 3 - KDBGER_DUMP_BYTE_PER_LINE)
#define KDBGER_DUMP_ABUF_SZ			KDBGER_BYTE_PER_SCREEN

#define KDBGER_STS_BUF_SZ			4096
#define KDBGER_STS_INTV_SECS		1
#define KDBGER_MAX_PCINAME			75


#define printWindow( RESRC, NAME, LINE, COLUMN, X, Y, COLORPAIR, FORMAT, ARGS... ) {\
    RESRC.NAME = newwin( LINE, COLUMN, X, Y );\
    RESRC.panel##NAME = new_panel( RESRC.NAME );\
    wbkgd( RESRC.NAME, COLOR_PAIR( COLORPAIR ) );\
    wattrset( RESRC.NAME, COLOR_PAIR( COLORPAIR ) | A_BOLD );\
    wprintw( RESRC.NAME, FORMAT, ##ARGS );\
    wattrset( RESRC.NAME, A_NORMAL );\
}


#define printWindowAt( RESRC, NAME, LINE, COLUMN, X, Y, COLORPAIR, FORMAT, ARGS... ) {\
    if( !RESRC.NAME ) {\
        RESRC.NAME = newwin( LINE, COLUMN, X, Y );\
        RESRC.panel##NAME = new_panel( RESRC.NAME );\
    }\
    wbkgd( RESRC.NAME, COLOR_PAIR( COLORPAIR ) );\
    wattrset( RESRC.NAME, COLOR_PAIR( COLORPAIR ) | A_BOLD );\
    mvwprintw( RESRC.NAME, 0, 0, FORMAT, ##ARGS );\
    wattrset( RESRC.NAME, A_NORMAL );\
}


#define printWindowMove( RESRC, NAME, LINE, COLUMN, X, Y, COLORPAIR, FORMAT, ARGS... ) {\
    if( RESRC.panel##NAME ) {\
        del_panel( RESRC.panel##NAME );\
        RESRC.panel##NAME = NULL;\
    }\
    if( RESRC.NAME ) {\
        delwin( RESRC.NAME );\
        RESRC.NAME = NULL;\
    }\
    RESRC.NAME = newwin( LINE, COLUMN, X, Y );\
    RESRC.panel##NAME = new_panel( RESRC.NAME );\
    wbkgd( RESRC.NAME, COLOR_PAIR( COLORPAIR ) );\
    wattrset( RESRC.NAME, COLOR_PAIR( COLORPAIR ) | A_BOLD );\
    mvwprintw( RESRC.NAME, 0, 0, FORMAT, ##ARGS );\
    wattrset( RESRC.NAME, A_NORMAL );\
}


#define destroyWindow( RESRC, NAME ) {\
    if( RESRC.panel##NAME ) {\
        del_panel( RESRC.panel##NAME );\
        RESRC.panel##NAME = NULL;\
    }\
    if( RESRC.NAME ) {\
        delwin( RESRC.NAME );\
        RESRC.NAME = NULL;\
    }\
}


typedef enum {

	KBPRS_ESC = 0x1B,
	KBPRS_UP = 0x103,
	KBPRS_DOWN = 0x102,
	KBPRS_LEFT = 0x104,
	KBPRS_RIGHT = 0x105,
	KBPRS_PGUP = 0x153,
	KBPRS_PGDN = 0x152,

	KBPRS_F1 = 0x109,
	KBPRS_F2 = 0x10A,
	KBPRS_F3 = 0x10B,
	KBPRS_F4 = 0x10C,
	KBPRS_F5 = 0x10D,
	KBPRS_F6 = 0x10E,
	KBPRS_F7 = 0x10F,
	KBPRS_F8 = 0x110,
	KBPRS_F9 = 0x111,
	KBPRS_F10 = 0x112,
	KBPRS_F11 = 0x113,
	KBPRS_F12 = 0x114,

	KBPRS_SPACE = 0x20,
	KBPRS_ENTER = 0x0A,

} kdbgerKeyPress_t;


typedef enum {

	KHF_INIT,
	KHF_MEM,
	KHF_IO,
	KHF_PCI,
	KHF_PCIL,
	KHF_IDE,
	KHF_CMOS,

} kdbgerHwFunc_t;


typedef enum {

    WHITE_RED = 1,
    WHITE_BLUE,
	WHITE_YELLOW,
	WHITE_BLACK,

    BLACK_WHITE,
	BLACK_GREEN,
	BLACK_YELLOW,
	BLACK_BLUE,

    CYAN_BLUE,
	CYAN_WHITE,

    YELLOW_BLUE,
    YELLOW_RED,
    YELLOW_BLACK,
	YELLOW_WHITE,

	RED_WHITE,
	RED_BLUE,

	MAGENTA_BLUE,

	GREEN_BLUE,
	GREEN_WHITE,

} kdbgerColorPairs_t;


typedef struct {

	s8					venTxt[ KDBGER_MAX_PCINAME ];
	s8					devTxt[ KDBGER_MAX_PCINAME ];

} kdbgerPciIds_t;


typedef struct {

    PANEL				*panelbackground;
    PANEL				*panellogo;
    PANEL				*panelcopyright;
    PANEL				*panelstatus;
    PANEL				*paneltime;
	PANEL				*panelhelp;

    WINDOW				*background;
    WINDOW				*logo;
    WINDOW				*copyright;
    WINDOW				*status;
    WINDOW				*time;
	WINDOW				*help;

	u32					lastSecond;
	s8					*statusStr;
	s32					strIdx;
	u8					toggleHelp;

} kdbgerBasePanel_t;


typedef struct {

	PANEL				*paneltop;
	PANEL				*paneloffset;
	PANEL				*panelleft;
	PANEL				*panelrtop;
    PANEL				*panelvalue;
    PANEL				*panelascii;
	PANEL				*panelinfo;
	PANEL				*panelhighlight;
	PANEL				*panelbits;
	PANEL				*panelbaseaddr;
	PANEL				*panelhlascii;
	PANEL				*panelftitle;
	PANEL				*panelstitle;

	WINDOW				*top;
	WINDOW				*offset;
	WINDOW				*left;
	WINDOW				*rtop;
    WINDOW				*value;
    WINDOW				*ascii;
	WINDOW				*info;
	WINDOW				*highlight;
	WINDOW 				*bits;
	WINDOW				*baseaddr;
	WINDOW				*hlascii;
	WINDOW				*ftitle;
	WINDOW				*stitle;

	u64					byteBase;
	s32					byteOffset;
	s8					*infoStr;
	u8					toggleBits;
	u8					toggleEditing;
	u8					editingBuf;

} kdbgerDumpPanel_t;


typedef struct {

	// General
	s32					fd;
	s32					inputBuf;
	kdbgerHwFunc_t		kdbgerHwFunc;
	kdbgerHwFunc_t		kdbgerPreviousHwFunc;

	// Packet
	u8					pktBuf[ KDBGER_MAXSZ_PKT ];
	kdbgerCommPkt_t		*pKdbgerCommPkt;

	// Base panel
	kdbgerBasePanel_t	kdbgerBasePanel;
	kdbgerDumpPanel_t	kdbgerDumpPanel;

	// PCI list
	kdbgerPciDev_t		*pKdbgerPciDev;
	kdbgerPciIds_t		*pKdbgerPciIds;
	u32					numOfPciDevice;
	s8					pciIdsPath[ KDBGER_MAX_PATH ];

	// E820 list
	kdbgerE820record_t	*pKdbgerE820record;
	u32					numOfE820Record;

} kdbgerUiProperty_t;


// Prototypes
s32 verifyResponsePacket( kdbgerCommPkt_t *pKdbgerCommPkt, kdbgerOpCode_t op );
s32 executeFunction( s32 fd, kdbgerOpCode_t op, u64 addr, u32 size, u8 *cntBuf, u8 *pktBuf, s32 lenPktBuf );
s32 connectToOluxOSKernel( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 readPciList( kdbgerUiProperty_t *pKdbgerUiProperty );
kdbgerPciDev_t *getPciDevice( kdbgerUiProperty_t *pKdbgerUiProperty, s32 num );
s32 readE820List( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 readMemory( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 writeMemoryByEditing( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 readIo( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 writeIoByEditing( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 readIde( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 writeIdeByEditing( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 readCmos( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 writeCmosByEditing( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 readPci( kdbgerUiProperty_t *pKdbgerUiProperty );
s32 writePciByEditing( kdbgerUiProperty_t *pKdbgerUiProperty );
u32 calculatePciAddress( u16 bus, u8 dev, u8 func );
s32 getPciVenDevTexts( u16 venid, u16 devid, s8 *ventxt, s8 *devtxt, s8 *pciids );

void printDumpBasePanel( kdbgerUiProperty_t *pKdbgerUiProperty );
void printDumpUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty );
void clearDumpBasePanel( kdbgerUiProperty_t *pKdbgerUiProperty );
void clearDumpUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty );
void handleKeyPressForDumpPanel( kdbgerUiProperty_t *pKdbgerUiProperty );

void printPciBasePanel( kdbgerUiProperty_t *pKdbgerUiProperty );
void printPciUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty );
void clearPciBasePanel( kdbgerUiProperty_t *pKdbgerUiProperty );
void clearPciUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty );
void handleKeyPressForPciPanel( kdbgerUiProperty_t *pKdbgerUiProperty );


