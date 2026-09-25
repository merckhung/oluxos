#define UART_BASE 0x10000000ULL

#define UART_RBR ((volatile unsigned char*)(UART_BASE + 0))
#define UART_THR ((volatile unsigned char*)(UART_BASE + 0))
#define UART_IER ((volatile unsigned char*)(UART_BASE + 1))
#define UART_LSR ((volatile unsigned char*)(UART_BASE + 5))

#define LSR_RX_READY (1 << 0)
#define LSR_TX_IDLE  (1 << 5)

void ns16550_putc(char c) {
    // Wait until THR is empty
    while ((*UART_LSR & LSR_TX_IDLE) == 0) {
        // Spin
    }
    *UART_THR = c;
}

void ns16550_puts(const char* s) {
    while (*s) {
        if (*s == '\n') {
            ns16550_putc('\r');
        }
        ns16550_putc(*s++);
    }
}

char ns16550_getc(void) {
    // Wait until data is ready
    while ((*UART_LSR & LSR_RX_READY) == 0) {
        // Spin
    }
    return *UART_RBR;
}

void ns16550_init(void) {
    // Disable interrupts
    *UART_IER = 0x00;
}
