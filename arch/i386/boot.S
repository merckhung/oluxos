/*
 * Copyright (C) 2006 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 *
 * boot.S -- OluxOS bootstrap code
 *
 *  1. initialize registers
 *  2. print booting message
 *  3. setup segment descriptors
 *  4. enable gate A20
 *  5. enable protected mode
 * 
 * LABEL    : Subroutine
 * _LABEL   : Replacement of flow control
 * __LABEL  : Replacement of variable
 *
 */


BOOTSEG     =   0x07C0
INITSEG     =   0x0500



/******************************************************************************
 * Code Segment
 */
.code16
.text
.global      _boot
    

_boot:

    //
    // Move boot code itself to 0x0400(1MB position)
    //
    // Setup DS:SI as source pointer
    //       ES:DI as destination pointer
    //
    // Size  512 bytes(Master Boot Record) 
    //       CX = 256
    //
    mov     $BOOTSEG, %ax
    mov     %ax, %ds

    mov     $INITSEG, %ax
    mov     %ax, %es

    mov     $0x100, %cx
    xor     %si, %si
    xor     %di, %di

    // startint move boot code
    rep     movsw

    // Jump to new location of boot code (0x400:_start)
    ljmp    $INITSEG, $(_start - _boot)

    

_start:

    // Setup new data segment address
    mov     $INITSEG, %ax
    mov     %ax, %ds

    
    // Print booting message
    xor     %ebx, %ebx
    mov     $(__bootmsg - _boot), %si
    mov     $__len_of_bootmsg, %dx
    call    sys_print


    // Show Busy state
    mov     $0x2fffff, %ecx
    call    sys_delay


    // Print done message
    mov     $(__donemsg - _boot), %si
    mov     $__len_of_donemsg, %dx
    call    sys_print


    // Print login message
    mov     $(__loginmsg - _boot), %si
    mov     $__len_of_loginmsg, %dx
    call    sys_print


    // Convert to linear address
    xorl    %eax, %eax
    movw    %ds, %ax
    shll    $4, %eax
    addl    $__gdt, %eax
    movl    %eax, (__gdtptr + 2)
    lgdt    __gdtptr


    // Disbale interrupt
    cli

    
    // Enable Gate A20
    in      $0x92, %al
    or      $0x02, %al
    out     %al, $0x92


    // Enable protected mode
    mov     %cr0, %eax
    or      $0x01, %eax
    mov     %eax, %cr0


    //ljmp    $INITSEG, $(_32bit_mode_start - _boot)


__loophere:
    jmp     __loophere




###############################################################################
# Subroutines -- General purpose
###############################################################################

/*
 * sys_print -- print string on console
 *
 * Input :
 *  SI      = Buffer pointer
 *  EBX     = Cursor position
 *  DX      = String length
 *
 * Output:
 *  EBX     = Lastest cursor position
 *
 * Note: 80x25 text mode
 *
 */
sys_print:


    // Save registers
    push    %ax
    push    %es


    // Setup ES to map VGA ram
    mov     $0xB800, %ax
    mov     %ax, %es


    // AL : ASCII code
    xor     %al, %al


_sys_print_loop:

    // get ASCII char
    movb    (%si), %al


    // is it a new-line char?
    cmp     $0xa, %al
    jnz     _sys_print_not_nl


    // handle ASCII new line
    mov     %bx, %ax
    mov     $80, %bl

    div     %bl

    inc     %al
    mul     %bl

    dec     %ax
    mov     %ax, %bx
    

    jmp _sys_print_next_char


_sys_print_not_nl:

    // Print on screen
    movb    %al, %es:(,%ebx,0x2)


_sys_print_next_char:

    // Pointer++
    inc     %si
    inc     %bx


    // Counter--
    dec     %dx
    jnz     _sys_print_loop


    // Restore registers
    pop     %es
    pop     %ax


    ret


/*
 * sys_delay -- Delay and show busy state
 *
 * Input :
 *  EBX     = Cursor position
 *  ECX     = Delay counter
 *  
 * Output:
 *  None
 */
sys_delay:

    // get pointer of character
    mov     $(__delaychar - _boot), %si

    // Plus offset (0 ~ 3)
    mov     %ecx, %eax
    and     $0x3, %ax
    add     %ax, %si

    // only print one char
    mov     $1, %dx

    // print busy char
    call    sys_print

    // rollback cursor
    dec     %ebx
    
    // counter--
    dec     %ecx
    jnz     sys_delay


    ret



###############################################################################
# Subroutines -- Interrupt Services
###############################################################################


/*
 * int80 -- interrupt serivce 0x80
 */
int80:

    // Setup ES with 0xB800 (Video memory)
    mov     $0xB800, %ax
    mov     %ax, %es

    xor     %bp, %bp
    xor     %si, %si

    // 'A'
    movb    $0x41, %al

_intlp:
    // Fill in ASCII code
    movb    %al, %es:(%bp, %si)

    // go to next ASCII code
    inc     %al

    // move to next byte (+2)
    inc     %si
    inc     %si

    // if( counter < 20 ) ?
    cmp     $3, %si
    jnz     _intlp

    iret


/*
 * install_int80 -- Install interrupt service 0x80
 */
install_int80:

    push    %ax
    push    %bx
    push    %es

    mov     $0, %ax
    mov     %ax, %es
    movw    $0x80, %bx

    // install Ooffset
    movw    $(int80 - _start), %es:(, %ebx, 4)

    // install segment
    movw    $INITSEG, %es:2(, %ebx, 4)

    pop     %es
    pop     %bx
    pop     %ax

    ret



###############################################################################
# 32-bit mode boot code
###############################################################################
.code32
_32bit_mode_start:

/*
    mov     $(__datasd - __gdt), %ax
    mov     %ax, %gs

    mov     $0xb800, %edi

    //mov     $0x41, %al
    //mov     %al, %gs:(%edi)
*/


_32loop_here:
    jmp     _32loop_here





/******************************************************************************
 * Data Segment
 */
.data
.align 16


/*
 * Globel Descriptors
 */
__gdt:

    // 2 NULL segment descriptors
    .fill 2, 8, 0

    //
    // Code segment descriptor
    // Base address = 0x0000h
    // Limit(15:00) = 0xffffh   (0xfffff, 4Gb limit)
    // Limit(19:16) = 0xfh      
    // P            = 1b        (present in memory)
    // DPL          = 00b       (privilege level 0)
    // S            = 1b        (Code/Data segment)
    // Type         = 1010b     (code, execute/read)
    // AVL          = 0b        (reserved for OS)
    // D/B          = 1b        (32-bit)
    // G            = 1b        (4kb, limit unit)
    //
    .word   0xffff
    .word   0x0000
    .word   0x9aff
    .word   0x00cf

    //
    // Data segment descriptor
    // Base address = 0x0000h
    // Limit(15:00) = 0xffffh   (0xfffff, 4Gb limit)
    // Limit(19:16) = 0xfh
    // P            = 1b        (present in memory)
    // DPL          = 00b       (privilege level 0)
    // S            = 1b        (Code/Data segment)
    // Type         = 0010b     (read/write)
    // AVL          = 0b
    // D/B          = 1b        (32-bit)
    // G            = 1b        (4kb, limit unit)
    //
__datasd:
    .word   0xffff
    .word   0x0000
    .word   0x92ff
    .word   0x00cf
    __len_of_gdt = . - __gdt


__gdtptr:

    //
    // GDT limit    = __len_of_gdt - 1
    // GDT Base     = 0x00000000h
    //
    .word       __len_of_gdt - 1
    .word       0, 0



/*
 * Messages
 */
.align 4
__bootmsg:
    .ascii      "Copyright (C) 2006 Olux Organization All rights reserved.\n"
    .ascii      "Welcome to Olux Operating System!\n"
    .ascii      "Author: Merck Hung <merck@olux.org>\n\n"

    .ascii      "Loading OluxOS......"
    __len_of_bootmsg = . - __bootmsg

__loginmsg:
    .ascii      "OluxOS login:"
    __len_of_loginmsg = . - __loginmsg

__donemsg:
    .ascii      "done\n\n\n\n"
    __len_of_donemsg = . - __donemsg

__delaychar:
    .ascii      "/-\\|"
    __len_of_delaychar = . - __delaychar


