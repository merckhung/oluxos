#include <types.h>
#include <riscv64/task.h>
#include <riscv64/interrupt.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>
#include "user_shell_bin_riscv64.h"

extern char _kernel_end;

#include <kernel/console.h>

void ns16550_init(void);
void ns16550_puts(const char* s);
void ns16550_putc(char c);
void thread_init(void);

void kputs(const char* s) {
    ns16550_puts(s);
}
int thread_create(void (*entry)(void));
void schedule(void);
void trap_init(void);

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

uint64_t translate_current_va(uint64_t va) {
    uint64_t satp_val;
    __asm__ volatile("csrr %0, satp" : "=r"(satp_val));
    if ((satp_val >> 60) == 0) return va;
    
    uint64_t root_phys = (satp_val & ((1ULL << 44) - 1)) << 12;
    uint64_t* l2 = (uint64_t*)root_phys;
    uint32_t l2_idx = (va >> 30) & 0x1FF;
    uint64_t l2_entry = l2[l2_idx];
    if ((l2_entry & PTE_V) == 0) return 0;

    // Check if L2 is leaf (1GB page)
    if (l2_entry & (PTE_R | PTE_W | PTE_X)) {
        uint64_t ppn = (l2_entry >> 10) & ((1ULL << 44) - 1);
        return (ppn << 12) | (va & 0x3FFFFFFF);
    }

    uint64_t* l1 = (uint64_t*)(((l2_entry >> 10) & ((1ULL << 44) - 1)) << 12);
    uint32_t l1_idx = (va >> 21) & 0x1FF;
    uint64_t l1_entry = l1[l1_idx];
    if ((l1_entry & PTE_V) == 0) return 0;

    // Check if L1 is leaf (2MB page)
    if (l1_entry & (PTE_R | PTE_W | PTE_X)) {
        uint64_t ppn = (l1_entry >> 10) & ((1ULL << 44) - 1);
        return (ppn << 12) | (va & 0x1FFFFF);
    }

    uint64_t* l0 = (uint64_t*)(((l1_entry >> 10) & ((1ULL << 44) - 1)) << 12);
    uint32_t l0_idx = (va >> 12) & 0x1FF;
    uint64_t l0_entry = l0[l0_idx];
    if ((l0_entry & PTE_V) == 0) return 0;

    uint64_t ppn = (l0_entry >> 10) & ((1ULL << 44) - 1);
    return (ppn << 12) | (va & 0xFFF);
}


void sbi_set_timer(uint64_t stime_value) {
    register uint64_t a0 __asm__("a0") = stime_value;
    register uint64_t a7 __asm__("a7") = 0; // SBI_SET_TIMER
    __asm__ volatile("ecall"
                     : "+r"(a0)
                     : "r"(a7)
                     : "memory");
}

#define SBI_EXT_HSM 0x48534D
#define SBI_HSM_HART_START 0

int sbi_hart_start(uint64_t hartid, uint64_t start_addr, uint64_t opaque) {
    register uint64_t a0 __asm__("a0") = hartid;
    register uint64_t a1 __asm__("a1") = start_addr;
    register uint64_t a2 __asm__("a2") = opaque;
    register uint64_t a6 __asm__("a6") = SBI_HSM_HART_START;
    register uint64_t a7 __asm__("a7") = SBI_EXT_HSM;
    __asm__ volatile("ecall"
                     : "+r"(a0)
                     : "r"(a1), "r"(a2), "r"(a6), "r"(a7)
                     : "memory");
    return (int)a0;
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
        if (scause == 8) { // Environment Call from U-mode (syscall)
            thread_set_current_regs(regs);

            uint64_t syscall_num = regs->gpr[17]; // a7
            uint64_t arg0 = regs->gpr[10];        // a0
            ns16550_puts("Syscall: ");
            print_hex(syscall_num);
            ns16550_puts(" arg0: ");
            print_hex(arg0);
            ns16550_puts("\n");
            uint64_t arg1 = regs->gpr[11];        // a1
            uint64_t arg2 = regs->gpr[12];        // a2

            if (syscall_num == 1) { // SYS_PUTCHAR
                ns16550_putc((char)arg0);
                regs->gpr[10] = 0;
            } else if (syscall_num == 2) { // SYS_SEND
                int ret = thread_ipc_send((uint32_t)arg0, (void*)arg1, (uint32_t)arg2);
                if (ret != IPC_BLOCKED) {
                    regs->gpr[10] = ret;
                }
            } else if (syscall_num == 3) { // SYS_RECV
                ns16550_puts("SYS_RECV enter tid=");
                print_hex(thread_get_current_tid());
                ns16550_puts(" regs=");
                print_hex((uint64_t)regs);
                ns16550_puts(" sepc=");
                print_hex(regs->sepc);
                ns16550_puts("\n");

                int ret = thread_ipc_recv((uint32_t)arg0, (void*)arg1, (uint32_t)arg2);

                ns16550_puts("SYS_RECV ret=");
                print_hex(ret);
                ns16550_puts(" a0=");
                print_hex(regs->gpr[10]);
                ns16550_puts("\n");

                if (ret != IPC_BLOCKED) {
                    regs->gpr[10] = ret;
                }
            } else if (syscall_num == 4) { // SYS_GETTID
                regs->gpr[10] = thread_get_current_tid();
            } else if (syscall_num == 5) { // SYS_MAP_MMIO
                regs->gpr[10] = (uint64_t)thread_map_mmio(arg0);
            } else if (syscall_num == 6) { // SYS_MAP_FB
                regs->gpr[10] = (uint64_t)thread_map_fb();
            } else if (syscall_num == 7) { // SYS_SPAWN
                regs->gpr[10] = thread_create_userspace((const unsigned char*)arg0, (uint32_t)arg1);
            } else if (syscall_num == 64) { // sys_write (Linux compatibility)
                if (arg0 == 1 || arg0 == 2) {
                    char* buf = (char*)arg1;
                    uint64_t i;
                    for (i = 0; i < arg2; i++) {
                        char c = buf[i];
                        if (c == '\n') ns16550_putc('\r');
                        ns16550_putc(c);
                    }
                    regs->gpr[10] = arg2;
                } else {
                    regs->gpr[10] = (uint64_t)-9; // EBADF
                }
            } else {
                ns16550_puts("\nUnknown syscall: ");
                print_hex(syscall_num);
                ns16550_puts("\n");
                regs->gpr[10] = -1;
            }

            // Advance PC past ecall instruction
            regs->sepc += 4;
        } else {
            ns16550_puts("\n!!! EXCEPTION !!!\n");
            ns16550_puts("tid:    "); print_hex(thread_get_current_tid());
            ns16550_puts("\nscause: "); print_hex(scause);
            ns16550_puts("\nsepc:   "); print_hex(sepc);
            ns16550_puts("\nstval:  "); print_hex(stval);
            ns16550_puts("\n");

            // Dump user registers
            ns16550_puts("USER REGS:\n");
            ns16550_puts("ra: "); print_hex(regs->gpr[1]);
            ns16550_puts(" sp: "); print_hex(regs->gpr[2]);
            ns16550_puts("\n");
            ns16550_puts("a0: "); print_hex(regs->gpr[10]);
            ns16550_puts(" a1: "); print_hex(regs->gpr[11]);
            ns16550_puts("\n");

            // Dump user stack if valid
            uint64_t usp = regs->gpr[2];
            ns16550_puts("USER STACK DUMP (sp=");
            print_hex(usp);
            ns16550_puts("):\n");
            
            // Align usp to 8 bytes and dump 16 words
            uint64_t usp_aligned = usp & ~7;
            int i;
            for (i = -4; i < 12; i++) {
                uint64_t va = usp_aligned + i * 8;
                uint64_t pa = translate_current_va(va);
                print_hex(va);
                ns16550_puts(": ");
                if (pa != 0) {
                    print_hex(*(uint64_t*)pa);
                } else {
                    ns16550_puts("INVALID_VA");
                }
                ns16550_puts("\n");
            }
            while(1);
        }
    }
}

int sbi_hart_start(uint64_t hartid, uint64_t start_addr, uint64_t opaque);
extern void _start(void);
extern void cpu_init(void);

void krn_entry(uint64_t hartid) {
    ns16550_init();
    ns16550_puts("\n\n");
    ns16550_puts("====================================\n");
    ns16550_puts(" OluxOS RISC-V 64-bit Starting...\n");
    ns16550_puts("====================================\n");
    ns16550_puts("Booted successfully to Supervisor Mode on Hart ");
    print_hex(hartid);
    ns16550_puts("\n");

    // Initialize CPU local state
    cpu_init();

    // Initialize PMM
    uint64_t mem_start = (uint64_t)&_kernel_end;
    uint64_t mem_size = 0x88000000 - mem_start;
    pmm_init(mem_start, mem_size);

    // Initialize Heap
    heap_init();

    // Test Heap
    ns16550_puts("Testing Heap...\n");
    void* p1 = kmalloc(100);
    void* p2 = kmalloc(200);
    ns16550_puts("kmalloc(100) = "); print_hex((uint64_t)p1); ns16550_puts("\n");
    ns16550_puts("kmalloc(200) = "); print_hex((uint64_t)p2); ns16550_puts("\n");
    kfree(p1);
    kfree(p2);
    ns16550_puts("Heap test passed.\n");

    // Enable SUM (Supervisor User Memory access) in sstatus
    uint64_t sstatus_val;
    __asm__ volatile("csrr %0, sstatus" : "=r"(sstatus_val));
    sstatus_val |= (1ULL << 18); // SUM is bit 18
    __asm__ volatile("csrw sstatus, %0" :: "r"(sstatus_val));

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
    
    // Create 4 userspace threads (which will run the shell binary as UART, Ramdisk, FS, Shell)
    thread_create_userspace(user_shell_bin, user_shell_bin_len);
    thread_create_userspace(user_shell_bin, user_shell_bin_len);
    thread_create_userspace(user_shell_bin, user_shell_bin_len);
    thread_create_userspace(user_shell_bin, user_shell_bin_len);
    
    // Boot secondary harts
    ns16550_puts("Booting secondary harts...\n");
    int i;
    for (i = 0; i < 4; i++) {
        if (i == hartid) continue;
        ns16550_puts("Starting Hart ");
        print_hex(i);
        ns16550_puts("...\n");
        int err = sbi_hart_start(i, (uint64_t)_start, 0);
        if (err != 0) {
            ns16550_puts("Failed to start Hart: ");
            print_hex(err);
            ns16550_puts("\n");
        }
    }

    ns16550_puts("Starting scheduler...\n");
    
    // Start scheduling
    while (1) {
        schedule();
    }
}

void krn_entry_secondary(uint64_t hartid) {
    // Enable SUM in sstatus for secondary hart
    uint64_t sstatus_val;
    __asm__ volatile("csrr %0, sstatus" : "=r"(sstatus_val));
    sstatus_val |= (1ULL << 18);
    __asm__ volatile("csrw sstatus, %0" :: "r"(sstatus_val));

    ns16550_puts("Hart ");
    print_hex(hartid);
    ns16550_puts(" booted successfully to secondary entry!\n");

    // Initialize traps for this hart
    trap_init();

    // Set first timer interrupt for this hart
    sbi_set_timer(read_time() + 100000);
    // Enable supervisor timer interrupt in sie
    __asm__ volatile("csrs sie, %0" :: "r"(1ULL << 5));

    // Enable interrupts globally
    IntEnable();

    // Start scheduling
    while (1) {
        schedule();
    }
}
