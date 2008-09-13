/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * handler.S -- OluxOS IA32 interrupt handler
 *
 */
#include <ia32/interrupt.h>


.global divide_error, debug, nmi, breakpoint, overflow, bound_range_exceeded
.global invalid_opcode, device_not_available, double_fault, coprocessor_segment_overrun
.global invalid_tss, segment_not_present, stack_fault, general_protection_exception
.global PageFaultHandler, x87_fpu_floating_point_error, alignment_check_exception
.global machine_check_exception, simd_floating_point_exception


//
// Pre-interrupt handling
//
.macro SaveAllRegsFirstStage
    movl    %eax, SavedAllRegs
    movl    %ecx, SavedAllRegs+4
    movl    %edx, SavedAllRegs+8
    movl    %ebx, SavedAllRegs+12
    movl    %ebp, SavedAllRegs+20
    movl    %esi, SavedAllRegs+24
    movl    %edi, SavedAllRegs+28
    movw    %ds, SavedAllRegs+48
    movw    %es, SavedAllRegs+52
    movw    %fs, SavedAllRegs+56
    movw    %gs, SavedAllRegs+60
.endm


.macro SaveErrorCode
    popl    %ebx
    movl    %ebx, SavedErrorCode
.endm


.macro SaveAllRegsSecondStage
    popl    %ebx
    movl    %ebx, SavedAllRegs+32	// EIP
    popl    %ebx
    movl    %ebx, SavedAllRegs+40	// CS
    popl    %ebx
    movl    %ebx, SavedAllRegs+36	// EFLAGS
    movw    %ss, SavedAllRegs+44
    movl    %esp, SavedAllRegs+16
.endm



//
// Restore all register after interrupt handler
//
.macro RestoreAllRegs
    movl    SavedAllRegs+16, %esp
    movw    SavedAllRegs+44, %ss
    movw    SavedAllRegs+60, %gs
    movw    SavedAllRegs+56, %fs
    movw    SavedAllRegs+52, %es
    movw    SavedAllRegs+48, %ds
    movl    SavedAllRegs+28, %edi
    movl    SavedAllRegs+24, %esi
    movl    SavedAllRegs+20, %ebp
    movl    SavedAllRegs+12, %ebx
    movl    SavedAllRegs+8, %edx
    movl    SavedAllRegs+4, %ecx
    movl    SavedAllRegs+36, %eax
    pushl   %eax					// EFLAGS
    movl    SavedAllRegs+40, %eax
    pushl   %eax					// CS
    movl    SavedAllRegs+32, %eax
    pushl   %eax					// EIP
    movl    SavedAllRegs, %eax
.endm



//
// Define
// Preliminary Interrupt Handler
// Preload interrupt vector into stack and then jump to common handler
//
.macro InstallIrqHandler
    .global IrqHandler_\@
    .align 4
    IrqHandler_\@:
    movl    $\@, -52(%esp)
    lea     -52(%esp), %ebx
    jmp     CommonIrqHandler
.endm



//
// Save All Registers
//
.align 4
.global SavedErrorCode
SavedErrorCode:
.long   0


.align 4
.global SavedAllRegs
SavedAllRegs:
.rept 16
.long   0
.endr



//
// Install IRQ Handlers
//
.rept NR_HW_IRQ
InstallIrqHandler
.endr


//
// CommonIrqHandler -- Common entry for HW IRQ interrupt
//
.align 4
CommonIrqHandler:

    SaveAllRegsFirstStage
	SaveAllRegsSecondStage

    subl    $4, %esp                // Get preload interrupt vector
    call    IntHandleIRQ
    addl    $4, %esp                // Discard vector
    jmp     InterruptReturn

InterruptReturn:
    RestoreAllRegs

    iret



//
// divide_error -- Handle exception 0 #DE
//
divide_error:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //movl    $0xb83be, %esi
    //movb    $'0', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x00
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// debug_error -- Handle exception 1 #DB
//
debug:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //movl    $0xb83be, %esi
    //movb    $'1', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x01
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// nmi -- Handle exception 2
//
nmi:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //movl    $0xb83be, %esi
    //movb    $'2', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x02
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// breakpoint -- Handle exception 3 #BP
//
breakpoint:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //movl    $0xb83be, %esi
    //movb    $'3', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x03
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// overflow -- Handle exception 4 #OF
//
overflow:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //movl    $0xb83be, %esi
    //movb    $'4', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x04
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// bound_range_exceeded -- Handle exception 5 #BR
//
bound_range_exceeded:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //movl    $0xb83be, %esi
    //movb    $'5', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x05
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// invalid_opcode -- Handle exception 6 #UD
//
invalid_opcode:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //movl    $0xb83be, %esi
    //movb    $'6', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x06
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// device_not_available -- Handle exception 7 #NM
//
device_not_available:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //movl    $0xb83be, %esi
    //movb    $'7', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x07
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// double_fault -- Handle exception 8 #DF
//
double_fault:

    SaveAllRegsFirstStage
    SaveErrorCode
    SaveAllRegsSecondStage


    //movl    $0xb83be, %esi
    //movb    $'8', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x08
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// coprocessor_segment_overrun -- Handle exception 9
//
coprocessor_segment_overrun:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //movl    $0xb83be, %esi
    //movb    $'9', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x09
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// invalid_tss -- Handle exception 10 #TS
//
invalid_tss:

    SaveAllRegsFirstStage
    SaveErrorCode
    SaveAllRegsSecondStage


    //movl    $0xb83bc, %esi
    //movb    $'1', (%esi)
    //movl    $0xb83be, %esi
    //movb    $'0', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x0A
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// segment_not_present -- Handle exception 11 #NP
//
segment_not_present:

    SaveAllRegsFirstStage
    SaveErrorCode
    SaveAllRegsSecondStage


    //movl    $0xb83bc, %esi
    //movb    $'1', (%esi)
    //movl    $0xb83be, %esi
    //movb    $'1', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x0B
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// stack_fault -- Handle exception 12 #SS
//
stack_fault:

    SaveAllRegsFirstStage
    SaveErrorCode
    SaveAllRegsSecondStage


    //movl    $0xb83bc, %esi
    //movb    $'1', (%esi)
    //movl    $0xb83be, %esi
    //movb    $'2', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x0C
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// general_protection_exception -- Handle exception 13 #NP
//
general_protection_exception:

    SaveAllRegsFirstStage
    SaveErrorCode
    SaveAllRegsSecondStage


    //movl    $0xb83bc, %esi
    //movb    $'1', (%esi)
    //movl    $0xb83be, %esi
    //movb    $'3', (%esi)


    //
    // FIXME: exception handle
    //
    pushl   $0x0D
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// Page-Fault Exception #PF (14)
//
PageFaultHandler:


	// Save all registers
    SaveAllRegsFirstStage
    SaveErrorCode
    SaveAllRegsSecondStage


	// Setup interrupt stack
	movl	$(InterruptStack + SZ_INT_STACK), %esp

	
	// Call Memory Management Code
	pushl	$SavedAllRegs
	pushl	SavedErrorCode
	call	MmPageFaultHandler


    RestoreAllRegs
    jmp     ExceptionReturn


//
// x87 FPU Floating-Point Error (#MF) -- Handle exception 16 #MF
//
x87_fpu_floating_point_error:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //
    // FIXME: exception handle
    //
    pushl   $0x10
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn 


//
// Alignment Check Exception (#AC) -- Handle exception 17 #AC
//
alignment_check_exception:

    SaveAllRegsFirstStage
    SaveErrorCode
    SaveAllRegsSecondStage


    //
    // FIXME: exception handle
    //
    pushl   $0x11
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn 


//
// Machine-Check Exception (#MC) -- Handle exception 18 #MC
//
machine_check_exception:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //
    // FIXME: exception handle
    //
    pushl   $0x12
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn 


//
// SIMD Floating-Point Exception (#XM) -- Handle exception 19 #XM
//
simd_floating_point_exception:

    SaveAllRegsFirstStage
    SaveAllRegsSecondStage


    //
    // FIXME: exception handle
    //
    pushl   $0x13
    call    GdbExceptionHandler


    RestoreAllRegs
    jmp     ExceptionReturn 


ExceptionReturn:

    iret



//
// ExceptionHandler -- Common entry for exception
//
.global ExceptionHandler
.align 4
ExceptionHandler:

	pushl	%esi
    movl    $0xb83bc, %esi
    incb    (%esi)
    movl    $0xb83bd, %esi
    incb    (%esi)
	popl	%esi

    iret



//
// Interrupt Stack
//
.common		InterruptStack, SZ_INT_STACK



