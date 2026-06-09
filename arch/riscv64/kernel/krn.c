#include <types.h>

void ns16550_init(void);
void ns16550_puts(const char* s);

void print_hex(uint64_t val) {
    char hex[17];
    int i;
    for (i = 15; i >= 0; i--) {
        int digit = val & 0xF;
        hex[i] = digit < 10 ? '0' + digit : 'A' + digit - 10;
        val >>= 4;
    }
    hex[16] = '\0';
    ns16550_puts("0x");
    ns16550_puts(hex);
}

void krn_entry(void) {
    ns16550_init();
    ns16550_puts("\n\n");
    ns16550_puts("====================================\n");
    ns16550_puts(" OluxOS RISC-V 64-bit Starting...\n");
    ns16550_puts("====================================\n");
    ns16550_puts("Booted successfully to Supervisor Mode!\n");
    
    // Hang for now
    while (1) {
        __asm__ volatile("wfi");
    }
}
