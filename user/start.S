.section ".text.start"
.global _start
_start:
#if defined(__riscv)
#if __riscv_xlen == 64
    ld      a0, 0(sp)
    addi    a1, sp, 8
#else
    lw      a0, 0(sp)
    addi    a1, sp, 4
#endif
    call    main
1:  j       1b
#else
    ldr     x0, [sp]
    add     x1, sp, #8
    bl      main
1:  b       1b
#endif
