#include <types.h>
#include <riscv64/task.h>

void ns16550_init(void);
void ns16550_puts(const char* s);
void thread_init(void);
int thread_create(void (*entry)(void));
void schedule(void);
void trap_init(void);
void IntEnable(void);
void IntDisable(void);

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

void sbi_set_timer(uint64_t stime_value) {
    register uint64_t a0 __asm__("a0") = stime_value;
    register uint64_t a7 __asm__("a7") = 0; // SBI_SET_TIMER
    __asm__ volatile("ecall"
                     : "+r"(a0)
                     : "r"(a7)
                     : "memory");
}

uint64_t read_time(void) {
    uint64_t t;
    __asm__ volatile("csrr %0, time" : "=r"(t));
    return t;
}

void trap_handler(RISCV64Registers* regs) {
    uint64_t scause = regs->scause;
    uint64_t sepc = regs->sepc;
    uint64_t stval = regs->sbadaddr;
    
    // Check if it is an interrupt (bit 63 is 1)
    if (scause & (1ULL << 63)) {
        uint64_t intr_id = scause & ~(1ULL << 63);
        if (intr_id == 5) { // Supervisor Timer Interrupt
            // Reset timer (100,000 ticks = 10ms on 10MHz clint)
            sbi_set_timer(read_time() + 100000);
            ns16550_puts("."); // Print dot to show timer interrupt is working!
        } else {
            ns16550_puts("\nUnexpected Interrupt: ");
            print_hex(intr_id);
            ns16550_puts("\n");
            while(1);
        }
    } else {
        // Exception
        ns16550_puts("\n!!! EXCEPTION !!!\n");
        ns16550_puts("scause: "); print_hex(scause);
        ns16550_puts("\nsepc:   "); print_hex(sepc);
        ns16550_puts("\nstval:  "); print_hex(stval);
        ns16550_puts("\n");
        while(1);
    }
}

void t1_entry(void) {
    int count = 0;
    while (1) {
        ns16550_puts("T1:");
        print_hex(count++);
        ns16550_puts(" ");
        // Yield to other threads
        schedule();
    }
}

void t2_entry(void) {
    int count = 0;
    while (1) {
        ns16550_puts("T2:");
        print_hex(count++);
        ns16550_puts(" ");
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

    // Initialize traps
    trap_init();
    
    // Set first timer interrupt
    sbi_set_timer(read_time() + 100000);
    // Enable supervisor timer interrupt in sie CSR (bit 5)
    __asm__ volatile("csrs sie, %0" :: "r"(1ULL << 5));
    
    // Enable interrupts globally in sstatus
    IntEnable();
    ns16550_puts("Traps and Timer initialized.\n");
    
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
