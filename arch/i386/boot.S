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

    .text
    .code16
    .global      _start
    
_start:
                jmp     _BootEntry 

_BootEntry:

                # initialize ds, es, ss with the value of cs
                mov     $0x7c00, %ax
                mov     %ax, %ds
                mov     $0x2000, %ax
                mov     %ax, %ss
                mov     $0x1000, %ax
                mov     %ax, %sp

_LOOP:
                call    PrintBootMessage
                mov     $250, %cx
_FOR:
                dec     %cx
                jcxz    _LOOP
                jmp     _FOR



PrintBootMessage:
                mov     $0x1301,%ax
                mov     $0x0007,%bx
                mov     $__LengthOfBootMessage,%cx
                mov     $_BootMessage,%bp
                int     $0x10
                ret

_BootMessage:
    .ascii      "\r\nLoading OluxOS kernel ......\r\n\r\n"
    __LengthOfBootMessage = . - _BootMessage
