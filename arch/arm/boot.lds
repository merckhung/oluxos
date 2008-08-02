OUTPUT_ARCH( armv5t )
ENTRY( _SystemBoot )

SECTIONS {

    .text (0x0) : {
    
        *(.text)
    }

    .data : {
                
        *(.data)
    }

    .bss : {

        *(.bss)
    }
}


