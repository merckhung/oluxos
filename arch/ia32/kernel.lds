OUTPUT_FORMAT( "binary" )
ENTRY( _setup_start )


SECTIONS {

    .text (0x00100000) : {

		arch/ia32/kernel/setup.o(.text)
        *(.text)
    }

    .data : {

        *(.data)
    }

    .bss : {

        *(.bss)
    }
}


