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


static s8 GdbInBuf[ GDB_BUF_LEN ];
static s8 GdbOutBuf[ GDB_BUF_LEN ];


extern u32 SavedAllRegs[ GDB_NUM_REGS ];
extern u32 SavedErrorCode;


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


u32 ConvertAllRegsToASCII( u32 *arr, u8 count, s8 *buf ) {

    s8 *pbuf = buf;

    for( ; count-- ; arr++ ) {

        pbuf += CbBinToAsciiBuf( *arr, pbuf, LOWERCASE, 0, 0 );
    }

    return (pbuf - buf);
}


void ConvertASCIIToAllRegs( u32 *arr, u8 count, s8 *buf ) {

    s8 *pbuf = buf;
    s8 tmp[ 9 ];

    for( ; count-- ; arr++, pbuf += 8 ) {
    
        CbStrCpy( tmp, pbuf, 8 );
        *arr = CbAsciiBufToBin( tmp );
    }
}


void GdbExceptionHandler( u32 ExceptionVector ) {

    s8 *p = GdbOutBuf, semicolon = ';', colon = ':';
    u8 sigval, step;


    DbgPrint( "Catched Exception No: %d\n", ExceptionVector );


    // Translate Exception to Signal
    sigval = TranslateException( ExceptionVector );


    // Signal
    *p++ = 'T';
    *p++ = CbBinToAscii( sigval >> 4, LOWERCASE );
    *p++ = CbBinToAscii( sigval & 0x0F, LOWERCASE );


    // ESP
    *p++ = CbBinToAscii( ESP, LOWERCASE );
    *p++ = colon;
    p   += CbBinToAsciiBuf( SavedAllRegs[ ESP ], p, LOWERCASE, 0, 0 );
    *p++ = semicolon;

    
    // EBP
    *p++ = CbBinToAscii( EBP, LOWERCASE );
    *p++ = colon;
    p   += CbBinToAsciiBuf( SavedAllRegs[ EBP ], p, LOWERCASE, 0, 0 );
    *p++ = semicolon;


    // ESP
    *p++ = CbBinToAscii( EIP, LOWERCASE );
    *p++ = colon;
    p   += CbBinToAsciiBuf( SavedAllRegs[ EIP ], p, LOWERCASE, 0, 0 );
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

                ConvertAllRegsToASCII( SavedAllRegs, GDB_NUM_REGS, p );
                break;


            // Set CPU registers
            case 'G':

                ConvertASCIIToAllRegs( SavedAllRegs, GDB_NUM_REGS, p );
                GdbOutBuf[ 0 ] = 'O';
                GdbOutBuf[ 1 ] = 'K';
                GdbOutBuf[ 2 ] = NULL;
                break;


            // Set one CPU register
            case 'P':

                DbgPrint( "Set one CPU Reg: %s\n", p );
                break;


            // Read Memory
            case 'm':

                DbgPrint( "Read Memory: %s\n", p );
                break;


            // Write Memory
            case 'M':

                DbgPrint( "Write Memory: %s\n", p );
                break;


            // Step one instruction
            case 's':

                step = 1;


            // Continue run
            case 'c':

                DbgPrint( "Step/Continue: %s\n", p );
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


