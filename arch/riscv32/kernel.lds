ENTRY(_start)

SECTIONS
{
    . = 0x80200000;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    . = ALIGN(16);
    _bss_start = .;
    .bss : {
        *(.bss .bss.*)
    }
    _bss_end = .;

    . = ALIGN(16);
    _stack_bottom = .;
    . = . + 0x4000; /* 16KB stack */
    _stack_top = .;
}
