ENTRY(_start)
SECTIONS {

    . = 0x7c00;

    .text : {
    
        *(.text)
    }

    .data : {
    
        *(.data)
    }

    .bss : {
    
        *(.bss)    
    }
}
