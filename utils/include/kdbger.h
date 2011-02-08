#define KDBGER_VERSION				"0.1"
#define KDBGER_MAX_PATH				40
#define KDBGER_DEF_TTYDEV			"/dev/pts/0"

#define KDBGER_MIN_LINE				0
#define KDBGER_MIN_COLUMN			0
#define KDBGER_MAX_LINE				23
#define KDBGER_MAX_COLUMN           80

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

#define KDBGER_INFO_MEMORY_BASE		KDBGER_INFO_BAR_FMT KDBGER_INFO_MEMORY_FUNC "\tAddress: "
#define KDBGER_INFO_IO_BASE			KDBGER_INFO_BAR_FMT KDBGER_INFO_IO_FUNC "\tAddress: "
#define KDBGER_INFO_PCI_BASE		KDBGER_INFO_BAR_FMT KDBGER_INFO_PCI_FUNC "\t "

#define KDBGER_INFO_MEMORY_BASE_FMT	"%8.8X-%8.8Xh"
#define KDBGER_INFO_IO_BASE_FMT		"%4.4Xh"
#define KDBGER_INFO_PCI_BASE_FMT	"Bus: %2.2Xh, Dev: %2.2Xh, Fun: %2.2Xh"

#define KDBGER_INFO_LINE			(KDBGER_MAX_LINE - 1)
#define KDBGER_INFO_COLUMN			0

#define KDBGER_DUMP_TOP_BAR			"0000 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F"
#define KDBGER_DUMP_LEFT_BAR		"000000100020003000400050006000700080009000A000B000C000D000E000F0"
#define KDBGER_DUMP_RTOP_BAR		"0123456789ABCDEF"

#define KDBGER_DUMP_TOP_LINE		4
#define KDBGER_DUMP_TOP_COLUMN		1

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


#define printWindow( RESRC, NAME, LINE, COLUMN, X, Y, COLORPAIR, FORMAT, ARGS... ) {\
    RESRC->NAME = newwin( LINE, COLUMN, X, Y );\
    RESRC->panel##NAME = new_panel( RESRC->NAME );\
    wbkgd( RESRC->NAME, COLOR_PAIR( COLORPAIR ) );\
    wattrset( RESRC->NAME, COLOR_PAIR( COLORPAIR ) | A_BOLD );\
    wprintw( RESRC->NAME, FORMAT, ##ARGS );\
    wattrset( RESRC->NAME, A_NORMAL );\
}


#define printWindowAt( RESRC, NAME, LINE, COLUMN, X, Y, COLORPAIR, FORMAT, ARGS... ) {\
    if( !RESRC->NAME ) {\
        RESRC->NAME = newwin( LINE, COLUMN, X, Y );\
        RESRC->panel##NAME = new_panel( RESRC->NAME );\
    }\
    wbkgd( RESRC->NAME, COLOR_PAIR( COLORPAIR ) );\
    wattrset( RESRC->NAME, COLOR_PAIR( COLORPAIR ) | A_BOLD );\
    mvwprintw( RESRC->NAME, 0, 0, FORMAT, ##ARGS );\
    wattrset( RESRC->NAME, A_NORMAL );\
}


#define printWindowMove( RESRC, NAME, LINE, COLUMN, X, Y, COLORPAIR, FORMAT, ARGS... ) {\
    if( RESRC->panel##NAME ) {\
        del_panel( RESRC->panel##NAME );\
        RESRC->panel##NAME = NULL;\
    }\
    if( RESRC->NAME ) {\
        delwin( RESRC->NAME );\
        RESRC->NAME = NULL;\
    }\
    RESRC->NAME = newwin( LINE, COLUMN, X, Y );\
    RESRC->panel##NAME = new_panel( RESRC->NAME );\
    wbkgd( RESRC->NAME, COLOR_PAIR( COLORPAIR ) );\
    wattrset( RESRC->NAME, COLOR_PAIR( COLORPAIR ) | A_BOLD );\
    mvwprintw( RESRC->NAME, 0, 0, FORMAT, ##ARGS );\
    wattrset( RESRC->NAME, A_NORMAL );\
}


#define destroyWindow( RESRC, NAME ) {\
    if( RESRC->panel##NAME ) {\
        del_panel( RESRC->panel##NAME );\
        RESRC->panel##NAME = NULL;\
    }\
    if( RESRC->NAME ) {\
        delwin( RESRC->NAME );\
        RESRC->NAME = NULL;\
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

	KBPRS_U_M = 'M',
	KBPRS_L_M = 'm',

	KBPRS_U_I = 'I',
	KBPRS_L_I = 'i',

	KBPRS_U_P = 'P',
	KBPRS_L_P = 'p',

	KBPRS_U_L = 'L',
	KBPRS_L_L = 'l',

} kdbgerKeyPress_t;


typedef enum {

	KHF_MEM,
	KHF_IO,
	KHF_PCI,
	KHF_PCIL,

} kdbgerHwFunc_t;


typedef enum {

    WHITE_RED = 1,
    WHITE_BLUE,
    BLACK_WHITE,
    CYAN_BLUE,
    RED_BLUE,
    YELLOW_BLUE,
    BLACK_GREEN,
    BLACK_YELLOW,
    YELLOW_RED,
    YELLOW_BLACK,
    WHITE_YELLOW,
	RED_WHITE,

} kdbgerColorPairs_t;


typedef struct {

    PANEL   *panelbackground;
    PANEL   *panellogo;
    PANEL   *panelcopyright;
    PANEL   *panelstatus;
    PANEL   *paneltime;

    WINDOW  *background;
    WINDOW  *logo;
    WINDOW  *copyright;
    WINDOW  *status;
    WINDOW  *time;

} kdbgerBasePanel_t;


typedef struct {

	PANEL *paneltop;
	PANEL *panelleft;
	PANEL *panelrtop;
    PANEL *panelvalue;
    PANEL *panelascii;
	PANEL *panelinfo;
	PANEL *panelhighlight;

	WINDOW *top;
	WINDOW *left;
	WINDOW *rtop;
    WINDOW *value;
    WINDOW *ascii;
	WINDOW *info;
	WINDOW *highlight;

} kdbgerDumpPanel_t, kdbgerMemoryPanel_t;


