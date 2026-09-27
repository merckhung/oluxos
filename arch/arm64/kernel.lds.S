#include <asm/memory.h>

OUTPUT_ARCH(aarch64)
ENTRY(_text)

SECTIONS
{
  . = KIMAGE_VADDR;

  .head.text : { KEEP(*(.head.text)) }

  .text : ALIGN(PAGE_SIZE) {
    _stext = .;
    *(.text.hot .text.hot.*)
    *(.text .text.*)
    *(.init.text)
    . = ALIGN(PAGE_SIZE);
    _etext = .;
  }

  .rodata : ALIGN(PAGE_SIZE) {
    __start_rodata = .;
    *(.rodata .rodata.*)
    . = ALIGN(8);
    __start_ex_table = .;
    KEEP(*(__ex_table))
    __stop_ex_table = .;
    . = ALIGN(8);
    __start_drivers = .;
    KEEP(*(.drivers))
    __stop_drivers = .;
    . = ALIGN(8);
    __start_pci_drivers = .;
    KEEP(*(.pci_drivers))
    __stop_pci_drivers = .;
    . = ALIGN(8);
    __start_usb_drivers = .;
    KEEP(*(.usb_drivers))
    __stop_usb_drivers = .;
    . = ALIGN(8);
    __start_initcalls = .;
    KEEP(*(SORT(.initcall.*)))
    __stop_initcalls = .;
    . = ALIGN(8);
    KEEP(*(.ksyms))
  }
  .rodata.end : ALIGN(PAGE_SIZE) { __end_rodata = .; }

  .data : ALIGN(PAGE_SIZE) {
    _sdata = .;
    *(.data .data.*)
    *(.init.data)
    _edata = .;
  }

  .pgtables (NOLOAD) : ALIGN(PAGE_SIZE) {
    __pgtables_start = .;
    *(.pgtables)
    . = ALIGN(PAGE_SIZE);
    __pgtables_end = .;
  }

  .bss (NOLOAD) : ALIGN(16) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
  }

  . = ALIGN(PAGE_SIZE);
  _end = .;

  /DISCARD/ : {
    *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr) *(.interp)
  }
}

_kernel_size_le = _end - _text;
ASSERT(_end - _text <= KIMAGE_MAX_SIZE, "kernel image too large for the boot page tables")
