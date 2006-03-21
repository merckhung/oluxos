########################################################################
#
# OluxOS bootstrap code
#   Startup from 16 bits mode
#
# Procedures:
# 1. initialize registers
# 2. print booting messages
# 3. switch processor into 32 bits protocted mode
# 4. hand over execution to OluxOS kernel
#

BOOTSECT=0x7c00

    .file       "boot.S"
    .text
    .code16
    .global      _start
    
_start:
                jmpl    $BOOTSECT, $_BootEntry 

_BootEntry:

                # initialize ds, es, ss with the value of cs
                xor     %ax, %ax
                mov     BOOTSECT, %ds
                mov     BOOTSECT, %ss
                mov     $0x2000, %sp
                call    PrintBootMessage


_LOOP:
                mov    $250,%cx
_FOR:
                dec     %cx
                jcxz    _NEXT
                jmp     _FOR
_NEXT:
                jmp     _LOOP



PrintBootMessage:
                mov     $0x1301,%ax
                mov     $0x0007,%bx
                mov     $__LengthOfBootMessage,%cx
                mov     $_BootMessage,%bp
                int     $0x10
                ret

_BootMessage:

    # 13 is ascii '\r', 10 is ascii '\n'
    .ascii      "\r\nLoading OluxOS kernel ......\r\n\r\n"
    __LengthOfBootMessage = . - _BootMessage
