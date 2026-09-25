OUTPUT_FORMAT( "elf64-x86-64" )
ENTRY( _setup_start )


SECTIONS {

    .text (0x00100000) : {

		*setup.o(.text)
        *(.text)
        *(.rodata*)
    }

    .data : {

        *(.data)
    }

    .bss : {

        *(.bss)
    }
}


