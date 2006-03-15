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


BOOTSEG     = 0x07c0


    .code16
    .text
    .global      _start
    
_start:
                jmpl    $BOOTSEG,$_BootEntry
                nop

_BootEntry:

                # initialize ds, es, ss with the value of cs
                xorw    %ax,%ax
                movw    %ax,%ds
                movw    %ax,%ss
                movw    $0x2000,%sp
                call    PrintBootMessage

                mov     $250,%cx

_LOOP:
                dec     %cx
                jcxz    _NEXT
                jmp     _LOOP
_NEXT:
                


                jmp     _BootEntry



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
