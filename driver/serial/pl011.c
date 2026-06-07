#if CONFIG_BOARD_RPI4
#define UART_BASE 0xFE201000ULL
#else
#define UART_BASE 0x09000000ULL
#endif
#define UART_DR ((volatile unsigned int*)(UART_BASE + 0x00))
#define UART_FR ((volatile unsigned int*)(UART_BASE + 0x18))

// Flag register bits
#define TXFF (1 << 5)
#define RXFE (1 << 4)

void pl011_putc(char c) {
  // Wait until transmit FIFO is not full
  while (*UART_FR & TXFF);
  *UART_DR = c;
}

void pl011_puts(const char* s) {
  while (*s) {
    if (*s == '\n') {
      pl011_putc('\r');
    }
    pl011_putc(*s++);
  }
}

char pl011_getc(void) {
  // Wait until receive FIFO is not empty
  while (*UART_FR & RXFE);
  return (char)(*UART_DR & 0xFF);
}

void pl011_init(void) {
  // QEMU usually initializes UART, but we can disable interrupts, set baud rate
  // etc. if needed. For Milestone 1, we assume it is already configured by
  // QEMU.
}
