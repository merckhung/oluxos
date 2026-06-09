.section ".text.start"
.global _start
_start:
#if defined(__riscv)
    call    main
1:  j       1b
#else
    bl      main
1:  b       1b
#endif
