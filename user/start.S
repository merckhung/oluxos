.section ".text.start"
.global _start
_start:
    bl      main
1:  b       1b
