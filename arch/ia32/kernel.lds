OUTPUT_FORMAT( "binary" )
ENTRY( _setup_start )


SECTIONS {

    .text (0x00100000) : {

        *(.text)
    }

    .data : {

        *(.data)
    }

    .bss : {

        *(.bss)
    }
}


