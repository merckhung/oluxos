.section ".text.boot"
.global _start

_start:
    // Setup stack pointer
    ldr     sp, =_stack_top

    // Clear BSS
    ldr     r0, =_bss_start
    ldr     r1, =_bss_end
    mov     r2, #0
.Lclear_bss:
    cmp     r0, r1
    bhs     .Ljump_kernel
    str     r2, [r0], #4
    b       .Lclear_bss

.Ljump_kernel:
    // Jump to krn_entry
    bl      krn_entry

.Lhang:
    wfe
    b       .Lhang

.section ".bss"
.align 4
_stack_limit:
    .space 4096
_stack_top:
