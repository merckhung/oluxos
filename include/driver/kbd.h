/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


//
// Controller
//
#define		KBD_IRQ			0x01
#define     KBCNT_PORT      0x60
#define     KB8042_PORT     0x64


//
// Keyboard Scan Code
//
#define     KEY_ESC         0x01
#define     KEY_ONE         0x02
#define     KEY_EXCLAMARK   0x02    // EXCLAMATION MARK '!'
#define     KEY_TWO         0x03
#define     KEY_AT          0x03    // AT SIGN '@'
#define     KEY_THREE       0x04
#define     KEY_POUND       0x04    // POUND SIGN '#'
#define     KEY_FOUR        0x05
#define     KEY_DOLLAR      0x05    // DOLLAR SIGN '$'
#define     KEY_FIVE        0x06
#define     KEY_PERCENT     0x06    // PERCENT SIGN '%'
#define     KEY_SIX         0x07
#define     KEY_CARET       0x07    // CARET '^'
#define     KEY_SEVEN       0x08
#define     KEY_AND         0x08    // AND '&'
#define     KEY_EIGHT       0x09
#define     KEY_ASTERISK    0x09    // '*'
#define     KEY_NINE        0x0a
#define     KEY_OPAREN      0x0a    // OPEN PARENTHESIS '('
#define     KEY_ZERO        0x0b
#define     KEY_CPAREN      0x0b    // CLOSE PARENTHESIS ')'
#define     KEY_DASH        0x0c    // '-'
#define     KEY_UNDERSCORE  0x0c    // '_'
#define     KEY_EQUAL       0x0d    // '='
#define     KEY_PLUS        0x0d    // '+'
#define     KEY_BKSP        0x0e    // BACK SPACE
#define     KEY_TAB         0x0f
#define     KEY_Q           0x10
#define     KEY_W           0x11
#define     KEY_E           0x12
#define     KEY_R           0x13
#define     KEY_T           0x14
#define     KEY_Y           0x15
#define     KEY_U           0x16
#define     KEY_I           0x17
#define     KEY_O           0x18
#define     KEY_P           0x19
#define     KEY_OBRACE      0x1a    // OPEN BRACE '{'
#define     KEY_OBRACKET    0x1a    // OPEN BRACKET '['
#define     KEY_CBRACE      0x1b    // CLOSE BRACE '}'
#define     KEY_CBRACKET    0x1b    // CLOSE BRACKET ']'
#define     KEY_ENTER       0x1c
#define     KEY_CTRL        0x1d
#define     KEY_A           0x1e
#define     KEY_S           0x1f
#define     KEY_D           0x20
#define     KEY_F           0x21
#define     KEY_G           0x22
#define     KEY_H           0x23
#define     KEY_J           0x24
#define     KEY_K           0x25
#define     KEY_L           0x26
#define     KEY_SEMICOLON   0x27    // ';'
#define     KEY_COLON       0x27    // ':'
#define     KEY_SQUOTE      0x28    // '
#define     KEY_QUOTE       0x28    // "
#define     KEY_APOST       0x29    // APOSTROPHE `
#define     KEY_TILDE       0x29    // '~'
#define     KEY_LSHIFT      0x2a    // LEFT SHIFT
#define     KEY_BACKSLASH   0x2b    // '\'
#define     KEY_PIPE        0x2b    // '|'
#define     KEY_Z           0x2c
#define     KEY_X           0x2d
#define     KEY_C           0x2e
#define     KEY_V           0x2f
#define     KEY_B           0x30
#define     KEY_N           0x31
#define     KEY_M           0x32
#define     KEY_COMMA       0x33    // ,
#define     KEY_LTHAN       0x33    // LESS THAN
#define     KEY_DOT         0x34    // .
#define     KEY_GTHAN       0x34    // GREATER THAN
#define     KEY_SLASH       0x35    // '/'
#define     KEY_QUESMARK    0x35    // QUESTION MARK
#define     KEY_RSHIFT      0x36    // RIGHT SHIFT
#define     KEY_PRTSCR      0x37    // PRINT SCREEN
#define     KEY_ALT         0x38
#define     KEY_SPACE       0x39
#define     KEY_CAPSLOCK    0x3a
#define     KEY_F1          0x3b
#define     KEY_F2          0x3c
#define     KEY_F3          0x3d
#define     KEY_F4          0x3e
#define     KEY_F5          0x3f
#define     KEY_F6          0x40
#define     KEY_F7          0x41
#define     KEY_F8          0x42
#define     KEY_F9          0x43
#define     KEY_F10         0x44
#define     KEY_NUMLOCK     0x45
#define     KEY_SCRLOCK     0x46    // SCROLL LOCK
#define     KEY_HOME        0x47
#define     KEY_UP          0x48
#define     KEY_PGUP        0x49    // PAGE UP
#define     KEY_MINUS       0x4a    // MINUS on keypad
#define     KEY_LEFT        0x4b
#define     KEY_CENTER      0x4c
#define     KEY_RIGHT       0x4d
#define     KEY_PLUSK       0x4e    // PLUS on keypad
#define     KEY_END         0x4f
#define     KEY_DOWN        0x50
#define     KEY_PGDN        0x51    // PAGE DOWN
#define     KEY_INSERT      0x52
#define     KEY_DELETE      0x53

#define     KEY_F11         0x57
#define     KEY_F12         0x58


#define     NO_SHIFT        0x00
#define     SHIFT_FREE      0x01
#define     SHIFT_HOLD      0x02


#define     NO_ASCII        0x00


struct KbdAsciiPair_t {

    u8    ShiftKey;
    u8    ScanCode;
    u8    AsciiCode;
};


void KbdInitKeyboard( void );
void KbdIntHandler( u8 irqnum );

void Kbd8042SendCmd( u8 cmd );
void KbdSendCmd( u8 cmd );


