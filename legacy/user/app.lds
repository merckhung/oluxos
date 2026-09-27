ENTRY(_start)
SECTIONS
{
    . = 0x00400000;
    .text : {
        *(.text.start)
        *(.text)
        *(.text.startup)
    }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
