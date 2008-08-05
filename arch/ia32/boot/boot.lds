OUTPUT_FORMAT( "binary" )
ENTRY( _SystemBoot )


SECTIONS {

    .text (0x7c00) : {
    
        *(.text)
    }

    .data : {

        *(.data)
    }

    .bss : {

        *(.bss)
    }
}


