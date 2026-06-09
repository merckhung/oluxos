#include <types.h>
#include <riscv32/task.h>

void ns16550_init(void);
void ns16550_puts(const char* s);
void ns16550_putc(char c);

void trap_init(void);
void IntEnable(void);
void IntDisable(void);

void print_hex(uint32_t val) {
    char hex[9];
    int i;
    for (i = 7; i >= 0; i--) {
        int digit = val & 0xF;
        hex[i] = digit < 10 ? '0' + digit : 'A' + digit - 10;
        val >>= 4;
    }
    hex[8] = '\0';
    ns16550_puts("0x");
    ns16550_puts(hex);
}

void task1(void) {
    int count = 0;
    while (1) {
        ns16550_puts("Task 1 running (count=");
        print_hex(count++);
        ns16550_puts(")\n");
        
        // Delay a bit
        volatile int i;
        for (i = 0; i < 1000000; i++);

        schedule(); // Yield
    }
}

void task2(void) {
    int count = 0;
    while (1) {
        ns16550_puts("Task 2 running (count=");
        print_hex(count++);
        ns16550_puts(")\n");
        
        // Delay a bit
        volatile int i;
        for (i = 0; i < 1000000; i++);

        schedule(); // Yield
    }
}

void krn_entry(void) {
    ns16550_init();
    ns16550_puts("====================================\n");
    ns16550_puts(" OluxOS RISC-V 32-bit Starting...\n");
    ns16550_puts("====================================\n");
    ns16550_puts("Booted successfully to Supervisor Mode!\n");
    
    // Initialize traps (not enabled globally yet)
    trap_init();

    // Initialize thread manager
    thread_init();

    // Create tasks
    ns16550_puts("Creating Task 1...\n");
    thread_create(task1);

    ns16550_puts("Creating Task 2...\n");
    thread_create(task2);

    ns16550_puts("Starting scheduler loop...\n");
    while (1) {
        schedule();
    }
}

void trap_handler(RISCV32Registers* regs) {
    ns16550_puts("\n!!! UNEXPECTED TRAP !!!\n");
    ns16550_puts("scause: "); print_hex(regs->scause);
    ns16550_puts("\nsepc:   "); print_hex(regs->sepc);
    ns16550_puts("\n");
    while(1);
}
