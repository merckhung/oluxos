OUTPUT_ARCH(arm)
ENTRY(_start)

SECTIONS {
    . = 0x40000000;

    .text : {
        *(.text.boot)
        *(.text)
        *(.rodata*)
    }

    .data : {
        *(.data)
    }

    .bss : {
        _bss_start = .;
        *(.bss)
        *(COMMON)
        _bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}
