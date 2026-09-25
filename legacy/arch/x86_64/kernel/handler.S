/*
 * handler.S -- OluxOS x86_64 interrupt handler
 */
#include <x86_64/interrupt.h>

.global divide_error, debug, nmi, breakpoint, overflow, bound_range_exceeded
.global invalid_opcode, device_not_available, double_fault, coprocessor_segment_overrun
.global invalid_tss, segment_not_present, stack_fault, general_protection_exception
.global PageFaultHandler, x87_fpu_floating_point_error, alignment_check_exception
.global machine_check_exception, simd_floating_point_exception

//
// Pre-interrupt handling
//
.macro SaveAllRegsFirstStage
    movq    %rax, SavedAllRegs
    movq    %rbx, SavedAllRegs+8
    movq    %rcx, SavedAllRegs+16
    movq    %rdx, SavedAllRegs+24
    movq    %rsi, SavedAllRegs+32
    movq    %rdi, SavedAllRegs+40
    movq    %rbp, SavedAllRegs+48
    movq    %r8,  SavedAllRegs+64
    movq    %r9,  SavedAllRegs+72
    movq    %r10, SavedAllRegs+80
    movq    %r11, SavedAllRegs+88
    movq    %r12, SavedAllRegs+96
    movq    %r13, SavedAllRegs+104
    movq    %r14, SavedAllRegs+112
    movq    %r15, SavedAllRegs+120
    movw    %ds,  SavedAllRegs+152
    movw    %es,  SavedAllRegs+156
    movw    %fs,  SavedAllRegs+160
    movw    %gs,  SavedAllRegs+164
.endm

.macro SaveErrorCode
    popq    %rbx
    movq    %rbx, SavedErrorCode
.endm

.macro SaveAllRegsSecondStage
    // In 64-bit mode, the CPU unconditionally pushes SS, RSP, RFLAGS, CS, RIP
    popq    %rbx
    movq    %rbx, SavedAllRegs+128 // RIP
    popq    %rbx
    movw    %bx,  SavedAllRegs+144 // CS
    popq    %rbx
    movq    %rbx, SavedAllRegs+136 // RFLAGS
    popq    %rbx
    movq    %rbx, SavedAllRegs+56 // RSP
    popq    %rbx
    movw    %bx,  SavedAllRegs+148 // SS

    // Setup interrupt stack
    movq    $(InterruptStack + SZ_INT_STACK), %rsp
.endm

//
// Restore all register after interrupt handler
//
.macro RestoreAllRegs
    movq    SavedAllRegs+56, %rsp
    xorq    %rbx, %rbx
    movw    SavedAllRegs+148, %bx
    pushq   %rbx // SS
    movq    SavedAllRegs+56, %rbx
    pushq   %rbx // RSP
    movq    SavedAllRegs+136, %rbx
    pushq   %rbx // RFLAGS
    xorq    %rbx, %rbx
    movw    SavedAllRegs+144, %bx
    pushq   %rbx // CS
    movq    SavedAllRegs+128, %rbx
    pushq   %rbx // RIP

    movw    SavedAllRegs+164, %gs
    movw    SavedAllRegs+160, %fs
    movw    SavedAllRegs+156, %es
    movw    SavedAllRegs+152, %ds
    movq    SavedAllRegs+120, %r15
    movq    SavedAllRegs+112, %r14
    movq    SavedAllRegs+104, %r13
    movq    SavedAllRegs+96,  %r12
    movq    SavedAllRegs+88,  %r11
    movq    SavedAllRegs+80,  %r10
    movq    SavedAllRegs+72,  %r9
    movq    SavedAllRegs+64,  %r8
    movq    SavedAllRegs+48,  %rbp
    movq    SavedAllRegs+40,  %rdi
    movq    SavedAllRegs+32,  %rsi
    movq    SavedAllRegs+24,  %rdx
    movq    SavedAllRegs+16,  %rcx
    movq    SavedAllRegs+8,   %rbx
    movq    SavedAllRegs,     %rax
.endm

//
// Define
// Preliminary Interrupt Handler
//
.macro InstallIrqHandler
    .global IrqHandler_\@
    .align 8
    IrqHandler_\@:
    movq    $\@, SavedIRQNumber
    jmp     CommonIrqHandler
.endm

//
// Save All Registers
//
.align 8
.global SavedAllRegs, SavedErrorCode
SavedErrorCode:
.quad   0
SavedIRQNumber:
.quad   0
SavedAllRegs:
.fill   168, 1, 0

//
// Install IRQ Handlers
//
.rept NR_HW_IRQ
InstallIrqHandler
.endr

//
// CommonIrqHandler -- Common entry for HW IRQ interrupt
//
.align 8
CommonIrqHandler:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage

    // Prepare input parameters for C calling convention
    movq    SavedIRQNumber, %rdi
    movq    $SavedAllRegs, %rsi
    call    IntHandleIRQ

InterruptReturn:
    RestoreAllRegs
    iretq

//
// divide_error -- Handle exception 0 #DE
//
divide_error:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x00, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn

//
// debug_error -- Handle exception 1 #DB
//
debug:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x01, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn

//
// nmi -- Handle exception 2
//
nmi:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x02, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn

//
// breakpoint -- Handle exception 3 #BP
//
breakpoint:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x03, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn

//
// overflow -- Handle exception 4 #OF
//
overflow:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x04, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn

//
// bound_range_exceeded -- Handle exception 5 #BR
//
bound_range_exceeded:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x05, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn

//
// invalid_opcode -- Handle exception 6 #UD
//
invalid_opcode:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x06, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn

//
// device_not_available -- Handle exception 7 #NM
//
device_not_available:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x07, %rdi
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
    movq    $0x08, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn

//
// coprocessor_segment_overrun -- Handle exception 9
//
coprocessor_segment_overrun:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x09, %rdi
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
    movq    $0x0A, %rdi
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
    movq    $0x0B, %rdi
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
    movq    $0x0C, %rdi
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
    movq    $0x0D, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn

//
// Page-Fault Exception #PF (14)
//
PageFaultHandler:
    SaveAllRegsFirstStage
    SaveErrorCode
    SaveAllRegsSecondStage
    movq    $SavedAllRegs, %rdi
    movq    SavedErrorCode, %rsi
    call    MmPageFaultHandler
    RestoreAllRegs
    jmp     ExceptionReturn

//
// x87 FPU Floating-Point Error (#MF) -- Handle exception 16 #MF
//
x87_fpu_floating_point_error:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x10, %rdi
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
    movq    $0x11, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn 

//
// Machine-Check Exception (#MC) -- Handle exception 18 #MC
//
machine_check_exception:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x12, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn 

//
// SIMD Floating-Point Exception (#XM) -- Handle exception 19 #XM
//
simd_floating_point_exception:
    SaveAllRegsFirstStage
    SaveAllRegsSecondStage
    movq    $0x13, %rdi
    call    GdbExceptionHandler
    RestoreAllRegs
    jmp     ExceptionReturn 

ExceptionReturn:
    iretq

//
// ExceptionHandler -- Common entry for exception
//
.global ExceptionHandler
.align 8
ExceptionHandler:
    pushq   %rsi
    movq    $0xb83bc, %rsi
    incb    (%rsi)
    movq    $0xb83bd, %rsi
    incb    (%rsi)
    popq    %rsi
    iretq

//
// Interrupt Stack
//
.common     InterruptStack, SZ_INT_STACK
