#include <types.h>

void ns16550_init(void);
void ns16550_puts(const char* s);
void thread_init(void);
int thread_create(void (*entry)(void));
void schedule(void);

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

void t1_entry(void) {
    int count = 0;
    while (1) {
        ns16550_puts("Thread 1 running, count=");
        print_hex(count++);
        ns16550_puts("\n");
        // Yield to other threads
        schedule();
    }
}

void t2_entry(void) {
    int count = 0;
    while (1) {
        ns16550_puts("Thread 2 running, count=");
        print_hex(count++);
        ns16550_puts("\n");
        // Yield to other threads
        schedule();
    }
}

void krn_entry(void) {
    ns16550_init();
    ns16550_puts("\n\n");
    ns16550_puts("====================================\n");
    ns16550_puts(" OluxOS RISC-V 64-bit Starting...\n");
    ns16550_puts("====================================\n");
    ns16550_puts("Booted successfully to Supervisor Mode!\n");
    
    // Initialize threads
    thread_init();
    
    // Create cooperative kernel threads
    thread_create(t1_entry);
    thread_create(t2_entry);
    
    ns16550_puts("Starting scheduler...\n");
    
    // Start scheduling (will switch to T1, then T2, etc.)
    while (1) {
        schedule();
    }
}
