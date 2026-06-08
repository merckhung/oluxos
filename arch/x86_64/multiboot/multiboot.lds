OUTPUT_FORMAT("elf32-i386")
ENTRY(_multiboot)

_linear_addr = 0x100000;

SECTIONS {

    .text _linear_addr : {

        *(.text)
    }

    .data : {
    
        *(.data)
    }

    .bss : {
    
        *(.bss)
    }
}
