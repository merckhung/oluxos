/*
 * Copyright (C) 2006 -  2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * gdb.c -- OluxOS IA32 Remote GDB Routines
 *
 */
#include <types.h>
#include <clib.h>
#include <ia32/io.h>
#include <ia32/gdb.h>
#include <ia32/debug.h>
#include <driver/serial.h>


#define SaveAllRegs()                   \
    __asm__ __volatile__ (              \
                                        \
        "movl   %eax, SaveRegs\n"       \
        "movl   %ecx, SaveRegs+4\n"     \
        "movl   %edx, SaveRegs+8\n"     \
        "movl   %ebx, SaveRegs+12\n"    \
        "movl   %ebp, SaveRegs+20\n"    \
        "movl   %esi, SaveRegs+24\n"    \
        "movl   %edi, SaveRegs+28\n"    \
        "xorw   %ax, %ax\n"             \
        "movw   %ax, SaveRegs+42\n"     \
        "movw   %ax, SaveRegs+46\n"     \
        "movw   %ds, SaveRegs+48\n"     \
        "movw   %ax, SaveRegs+50\n"     \
        "movw   %es, SaveRegs+52\n"     \
        "movw   %ax, SaveRegs+54\n"     \
        "movw   %fs, SaveRegs+56\n"     \
        "movw   %ax, SaveRegs+58\n"     \
        "movw   %gs, SaveRegs+60\n"     \
        "movw   %ax, SaveRegs+62\n"     \
        "popl   %ebx\n"                 \
        "movl   %ebx, SaveErrCode\n"    \
        "popl   %ebx\n"                 \
        "movl   %ebx, SaveRegs+32\n"    \
        "popl   %ebx\n"                 \
        "movl   %ebx, SaveRegs+40\n"    \
        "popl   %ebx\n"             \
        "movl   %ebx, SaveRegs+36\n"    \
        "movw   %ss, SaveRegs+44\n"     \
        "movl   %esp, SaveRegs+16\n"    \
    );


#define RestoreAllRegs()                \
    __asm__ __volatile__ (              \
                                        \
        "movl   SaveRegs+16, %esp\n"    \
        "movw   SaveRegs+44, %ss\n"     \
        "movw   SaveRegs+60, %gs\n"     \
        "movw   SaveRegs+56, %fs\n"     \
        "movw   SaveRegs+52, %es\n"     \
        "movw   SaveRegs+48, %ds\n"     \
        "movl   SaveRegs+28, %edi\n"    \
        "movl   SaveRegs+24, %esi\n"    \
        "movl   SaveRegs+20, %ebp\n"    \
        "movl   SaveRegs+12, %ebx\n"    \
        "movl   SaveRegs+8, %edx\n"     \
        "movl   SaveRegs+4, %ecx\n"     \
        "movl   SaveRegs+36, %eax\n"    \
        "pushl  %eax\n"                 \
        "movl   SaveRegs+40, %eax\n"    \
        "pushl  %eax\n"                 \
        "movl   SaveRegs+32, %eax\n"    \
        "pushl  %eax\n"                 \
        "movl   SaveRegs, %eax\n"       \
    );


u32 SaveRegs[ 16 ];
u32 SaveErrCode;


static s8 GdbInBuf[ GDB_BUF_LEN ];
static s8 GdbOutBuf[ GDB_BUF_LEN ];

static u32 ExcTranTbl[][ 2 ] = {

    { 0,    8   },
    { 1,    5   },
    { 1,    5   },
    { 3,    5   },
    { 4,    16  },
    { 5,    16  },
    { 6,    4   },
    { 7,    8   },
    { 8,    7   },
    { 9,    11  },
    { 10,   11  },
    { 11,   11  },
    { 12,   11  },
    { 13,   11  },
    { 14,   11  },
    { 16,   7   },
    { ~0,   7   }
};


void GdbInit( void ) {

    SrInit();
}


void GdbPutChar( s8 c ) {

    SrPutChar( c );
}


s8 GdbGetChar( void ) {

    return SrGetChar();
}


s8 *GdbGetPacket( s8 *buf ) {

    s8 c;
    u8 cksm, xcksm;
    u32 i;


    while( 1 ) {
    

        do {
        
            c = GdbGetChar();
        } while( c != '$' );


        for( i = 0, cksm = 0 ; i < GDB_BUF_LEN ; i++ ) {
        

            // Receive character
            c = GdbGetChar();
            if( c == '#' ) {

                // End of packet
                break;
            }


            // Restart buffer
            if( c == '$' ) {
            
                i = 0;
                cksm = 0;
                continue;
            }


            // Update checksum
            cksm += c;

            
            // Write to buffer
            buf[ i ] = c;
        }

        
        // Write NULL to the end
        buf[ i ] = NULL;


        if( c == '#' ) {


            c = GdbGetChar();
            xcksm = CbAsciiToBin( c ) << 4;
            c = GdbGetChar();
            xcksm = CbAsciiToBin( c );

            
            // Validate checksum
            if( xcksm != cksm ) {
            

                // Checksum failed
                GdbPutChar( '-' );
            }
            else {
            

                // Checksum Ok
                GdbPutChar( '+' );


                // Reply sequence ID
                if( buf[ 2 ] == ':' ) {
                
                    GdbPutChar( buf[ 0 ] );
                    GdbPutChar( buf[ 1 ] );

                    return (buf + 3);
                }


                return buf;
            }
        }
    }
}


void GdbSendPacket( s8 *buf ) {

    u8 cksm;
    u32 i;


    do {

        // Header
        GdbPutChar( '$' );


        // Content
        for( i = 0, cksm = 0 ; buf[ i ] ; i++ ) {
        
            GdbPutChar( buf[ i ] );
            cksm += buf[ i ];
        }


        // Tail
        GdbPutChar( '#' );

        // Checksum
        GdbPutChar( CbBinToAscii( cksm >> 4, LOWERCASE ) );
        GdbPutChar( CbBinToAscii( cksm & 0x0F, LOWERCASE ) );
    
    
    } while( GdbGetChar() != '+' );
}


u8 TranslateException( u32 ExceptionVector ) {

    u8 i;

    for( i = 0 ; ; i++ ) {

        
        // Not found
        if( (ExcTranTbl[ i ][ 0 ] == ~0) || (ExceptionVector == ExcTranTbl[ i ][ 0 ]) ) {
        
            return ExcTranTbl[ i ][ 1 ] & 0xFF;
        }
    }


    return 7;
}


void GdbExceptionHandler( u32 ExceptionVector ) {

    s8 *p = GdbOutBuf, semicolon = ';', colon = ':';
    u8 sigval, step;


    // Translate Exception to Signal
    sigval = TranslateException( ExceptionVector );


    // Signal
    *p++ = 'T';
    *p++ = CbBinToAscii( sigval >> 4, LOWERCASE );
    *p++ = CbBinToAscii( sigval & 0x0F, LOWERCASE );


    // ESP
    *p++ = CbBinToAscii( ESP, LOWERCASE );
    *p++ = colon;
    p   += CbBinToAsciiBuf( SaveRegs[ ESP ], p, LOWERCASE, 0, 0 );
    *p++ = semicolon;

    
    // EBP
    *p++ = CbBinToAscii( EBP, LOWERCASE );
    *p++ = colon;
    p   += CbBinToAsciiBuf( SaveRegs[ EBP ], p, LOWERCASE, 0, 0 );
    *p++ = semicolon;


    // ESP
    *p++ = CbBinToAscii( EIP, LOWERCASE );
    *p++ = colon;
    p   += CbBinToAsciiBuf( SaveRegs[ EIP ], p, LOWERCASE, 0, 0 );
    *p++ = semicolon;


    // Terminate Char
    *p++ = NULL;


    // Send Packet
    GdbSendPacket( GdbOutBuf );


    // Default step = 0
    step = 0;


    while( 1 ) {
    

        // Clear output buffer
        GdbOutBuf[ 0 ] = NULL;


        // Receive packet
        p = GdbGetPacket( GdbInBuf );


        switch( *p++ ) {
        

            case '?':

                GdbOutBuf[ 0 ] = 'S';
                GdbOutBuf[ 0 ] = CbBinToAscii( sigval >> 4, LOWERCASE );
                GdbOutBuf[ 0 ] = CbBinToAscii( sigval & 0x0F, LOWERCASE );
                GdbOutBuf[ 0 ] = NULL;
                break;


            // Debug Flag
            case 'd':

                DbgPrint( "Remote GDB OP=d, Not Implemented\n" );
                break;
              

            // Return CPU registers
            case 'g':


                break;


            // Set CPU registers
            case 'G':


                break;


            // Set one CPU register
            case 'P':


                break;


            // Read Memory
            case 'm':


                break;


            // Write Memory
            case 'M':

                
                break;


            // Step one instruction
            case 's':

                step = 1;


            // Continue run
            case 'c':


                break;


            // Kill the program
            case 'k':

                DbgPrint( "Remote GDB OP=k, Not Implemented\n" );
                break;


            default:

                DbgPrint( "Remote GDB: Unrecognized Operation\n" );
                break;
        }


        // Send packet, reply the request
        GdbSendPacket( GdbOutBuf );
    }
}


