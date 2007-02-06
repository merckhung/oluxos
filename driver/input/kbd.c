/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * d.c -- OluxOS IA32 keyboard routines
 *
 */
#include <types.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/debug.h>
#include <driver/kbd.h>
#include <driver/console.h>


extern void PreliminaryInterruptHandler_1( void );
static __u8 CapsLock = 0;
static __u8 NumLock = 0;
static __u8 ScrollLock = 0;


static struct KbdAsciiPair_t kap[] = {

    { NO_SHIFT      , KEY_ESC           , NO_ASCII      },
    { SHIFT_FREE    , KEY_ONE           , '1'           },
    { SHIFT_HOLD    , KEY_EXCLAMARK     , '!'           },
    { SHIFT_FREE    , KEY_TWO           , '2'           },
    { SHIFT_HOLD    , KEY_AT            , '@'           },
    { SHIFT_FREE    , KEY_THREE         , '3'           },
    { SHIFT_HOLD    , KEY_POUND         , '#'           },
    { SHIFT_FREE    , KEY_FOUR          , '4'           },
    { SHIFT_HOLD    , KEY_DOLLAR        , '$'           },
    { SHIFT_FREE    , KEY_FIVE          , '5'           },
    { SHIFT_HOLD    , KEY_PERCENT       , '%'           },
    { SHIFT_FREE    , KEY_SIX           , '6'           },
    { SHIFT_HOLD    , KEY_CARET         , '^'           },
    { SHIFT_FREE    , KEY_SEVEN         , '7'           },
    { SHIFT_HOLD    , KEY_AND           , '&'           },
    { SHIFT_FREE    , KEY_EIGHT         , '8'           },
    { SHIFT_HOLD    , KEY_ASTERISK      , '*'           },
    { SHIFT_FREE    , KEY_NINE          , '9'           },
    { SHIFT_HOLD    , KEY_OPAREN        , '('           },
    { SHIFT_FREE    , KEY_ZERO          , '0'           },
    { SHIFT_HOLD    , KEY_CPAREN        , ')'           },
    { SHIFT_FREE    , KEY_DASH          , '-'           },
    { SHIFT_HOLD    , KEY_UNDERSCORE    , '_'           },
    { SHIFT_FREE    , KEY_EQUAL         , '='           },
    { SHIFT_HOLD    , KEY_PLUS          , '+'           },
    { NO_SHIFT      , KEY_BKSP          , NO_ASCII      },
    { NO_SHIFT      , KEY_TAB           , NO_ASCII      },
    { SHIFT_FREE    , KEY_Q             , 'q'           },
    { SHIFT_HOLD    , KEY_Q             , 'Q'           },
    { SHIFT_FREE    , KEY_W             , 'w'           },
    { SHIFT_HOLD    , KEY_W             , 'W'           },
    { SHIFT_FREE    , KEY_E             , 'e'           },
    { SHIFT_HOLD    , KEY_E             , 'E'           },
    { SHIFT_FREE    , KEY_R             , 'r'           },
    { SHIFT_HOLD    , KEY_R             , 'R'           },
    { SHIFT_FREE    , KEY_T             , 't'           },
    { SHIFT_HOLD    , KEY_T             , 'T'           },
    { SHIFT_FREE    , KEY_Y             , 'y'           },
    { SHIFT_HOLD    , KEY_Y             , 'Y'           },
    { SHIFT_FREE    , KEY_U             , 'u'           },
    { SHIFT_HOLD    , KEY_U             , 'U'           },
    { SHIFT_FREE    , KEY_I             , 'i'           },
    { SHIFT_HOLD    , KEY_I             , 'I'           },
    { SHIFT_FREE    , KEY_O             , 'o'           },
    { SHIFT_HOLD    , KEY_O             , 'O'           },
    { SHIFT_FREE    , KEY_P             , 'p'           },
    { SHIFT_HOLD    , KEY_P             , 'P'           },
    { SHIFT_FREE    , KEY_OBRACE        , '['           },
    { SHIFT_HOLD    , KEY_OBRACKET      , '{'           },
    { SHIFT_FREE    , KEY_CBRACE        , ']'           },
    { SHIFT_HOLD    , KEY_CBRACKET      , '}'           },
    { NO_SHIFT      , KEY_ENTER         , '\n'          },
    { SHIFT_FREE    , KEY_A             , 'a'           },
    { SHIFT_HOLD    , KEY_A             , 'A'           },
    { SHIFT_FREE    , KEY_S             , 's'           },
    { SHIFT_HOLD    , KEY_S             , 'S'           },
    { SHIFT_FREE    , KEY_D             , 'd'           },
    { SHIFT_HOLD    , KEY_D             , 'D'           },
    { SHIFT_FREE    , KEY_F             , 'f'           },
    { SHIFT_HOLD    , KEY_F             , 'F'           },
    { SHIFT_FREE    , KEY_G             , 'g'           },
    { SHIFT_HOLD    , KEY_G             , 'G'           },
    { SHIFT_FREE    , KEY_H             , 'h'           },
    { SHIFT_HOLD    , KEY_H             , 'H'           },
    { SHIFT_FREE    , KEY_J             , 'j'           },
    { SHIFT_HOLD    , KEY_J             , 'J'           },
    { SHIFT_FREE    , KEY_K             , 'k'           },
    { SHIFT_HOLD    , KEY_K             , 'K'           },
    { SHIFT_FREE    , KEY_L             , 'l'           },
    { SHIFT_HOLD    , KEY_L             , 'L'           },
    { SHIFT_FREE    , KEY_SEMICOLON     , ';'           },
    { SHIFT_HOLD    , KEY_COLON         , ':'           },
    { SHIFT_FREE    , KEY_SQUOTE        , '\''          },
    { SHIFT_HOLD    , KEY_QUOTE         , '"'           },
    { SHIFT_FREE    , KEY_APOST         , '`'           },
    { SHIFT_HOLD    , KEY_TILDE         , '~'           },
    { SHIFT_FREE    , KEY_BACKSLASH     , '\\'          },
    { SHIFT_HOLD    , KEY_PIPE          , '|'           },
    { SHIFT_FREE    , KEY_Z             , 'z'           },
    { SHIFT_HOLD    , KEY_Z             , 'Z'           },
    { SHIFT_FREE    , KEY_X             , 'x'           },
    { SHIFT_HOLD    , KEY_X             , 'X'           },
    { SHIFT_FREE    , KEY_C             , 'c'           },
    { SHIFT_HOLD    , KEY_C             , 'C'           },
    { SHIFT_FREE    , KEY_V             , 'v'           },
    { SHIFT_HOLD    , KEY_V             , 'V'           },
    { SHIFT_FREE    , KEY_B             , 'b'           },
    { SHIFT_HOLD    , KEY_B             , 'B'           },
    { SHIFT_FREE    , KEY_N             , 'n'           },
    { SHIFT_HOLD    , KEY_N             , 'N'           },
    { SHIFT_FREE    , KEY_M             , 'm'           },
    { SHIFT_HOLD    , KEY_M             , 'M'           },
    { SHIFT_FREE    , KEY_COMMA         , ','           },
    { SHIFT_HOLD    , KEY_LTHAN         , '<'           },
    { SHIFT_FREE    , KEY_DOT           , '.'           },
    { SHIFT_HOLD    , KEY_GTHAN         , '>'           },
    { SHIFT_FREE    , KEY_SLASH         , '/'           },
    { SHIFT_HOLD    , KEY_QUESMARK      , '?'           },
    { NO_SHIFT      , KEY_PRTSCR        , NO_ASCII      },
    { NO_SHIFT      , KEY_SPACE         , ' '           },
    { NO_SHIFT      , KEY_F1            , NO_ASCII      },
    { NO_SHIFT      , KEY_F2            , NO_ASCII      },
    { NO_SHIFT      , KEY_F3            , NO_ASCII      },
    { NO_SHIFT      , KEY_F4            , NO_ASCII      },
    { NO_SHIFT      , KEY_F5            , NO_ASCII      },
    { NO_SHIFT      , KEY_F6            , NO_ASCII      },
    { NO_SHIFT      , KEY_F7            , NO_ASCII      },
    { NO_SHIFT      , KEY_F8            , NO_ASCII      },
    { NO_SHIFT      , KEY_F9            , NO_ASCII      },
    { NO_SHIFT      , KEY_F10           , NO_ASCII      },
    { NO_SHIFT      , KEY_HOME          , NO_ASCII      },
    { NO_SHIFT      , KEY_UP            , NO_ASCII      },
    { NO_SHIFT      , KEY_PGUP          , NO_ASCII      },
    { NO_SHIFT      , KEY_MINUS         , '-'           },
    { NO_SHIFT      , KEY_LEFT          , NO_ASCII      },
    { NO_SHIFT      , KEY_CENTER        , NO_ASCII      },
    { NO_SHIFT      , KEY_RIGHT         , NO_ASCII      },
    { NO_SHIFT      , KEY_PLUSK         , '+'           },
    { NO_SHIFT      , KEY_END           , NO_ASCII      },
    { NO_SHIFT      , KEY_DOWN          , NO_ASCII      },
    { NO_SHIFT      , KEY_PGDN          , NO_ASCII      },
    { NO_SHIFT      , KEY_INSERT        , NO_ASCII      },
    { NO_SHIFT      , KEY_DELETE        , NO_ASCII      },
    { NO_SHIFT      , KEY_F11           , NO_ASCII      },
    { NO_SHIFT      , KEY_F12           , NO_ASCII      },
    { 0             , 0                 , 0             },
};



//
// KbdInitKeyboard
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Initialize onboard 8042 and keyboard controller
//
void KbdInitKeyboard( void ) {

    IntDisable();
    IntRegIRQ( 1, IRQHandler( 1 ), KbdIntHandler );
    IntEnable();
}


//
// KbdIntHandler
//
// Input:
//  irqnum  : IRQ number
//
// Return:
//  None
//
// Description:
//  Keyboard interrupt handler
//
void KbdIntHandler( __u8 irqnum ) {

    __u16 i;
    __u8 keycode;


    // Disable interrupt
    IntDisable();


    // Read key code
    keycode = IoInByte( 0x60 );
    if( keycode & 0x80 ) {
    
        goto KbdIntHandler_Done;         
    }


    switch( keycode ) {
    
        case KEY_CAPSLOCK:
            KbdSendCmd( 0xed );
            KbdSendCmd( 0x04 );
            CapsLock = ~(CapsLock & 0x01);
            goto KbdIntHandler_Done;

        case KEY_NUMLOCK:
            KbdSendCmd( 0xed );
            KbdSendCmd( 0x02 );
            NumLock = ~(NumLock & 0x01 );
            goto KbdIntHandler_Done;

        case KEY_SCRLOCK:
            KbdSendCmd( 0xed );
            KbdSendCmd( 0x01 );
            ScrollLock = ~(ScrollLock & 0x01 );
            goto KbdIntHandler_Done;
    }


    for( i = 0 ; kap[ i ].ScanCode ; i++ ) {
    
        if( kap[ i ].ScanCode == keycode ) {
        
            if( kap[ i ].AsciiCode != NO_ASCII ) {

                DbgPrint( "%c", kap[ i ].AsciiCode );
            }
            break;
        }
    }


KbdIntHandler_Done:

    IoOutByte( 0x20, 0x20 );
    IntEnable();
}


//
// Kbd8042SendCmd
//
// Input:
//  cmd     : Command of onboard 8042 controller
//
// Return:
//  None
//
// Description:
//  Send command to onboard 8042 controller
//
void Kbd8042SendCmd( __u8 cmd ) {

    // Wait for input buffer empty
    while( IoInByte( KB8042_PORT ) & 0x02 );

    IoOutByte( cmd, KB8042_PORT );

    // Wait for pc 8042 controller
    while( IoInByte( KB8042_PORT ) & 0x02 );
}


//
// KbdSendCmd
//
// Input:
//  cmd     : Command of keyboard controller
//
// Return:
//  None
//
// Description:
//  Send command to keyboard controller
//
void KbdSendCmd( __u8 cmd ) {

    // Disable keyboard
    Kbd8042SendCmd( 0xad );

    // Send command to keyboard controller
    IoOutByte( cmd, KBCNT_PORT );

    // Reenable keyboard
    Kbd8042SendCmd( 0xae );
}


