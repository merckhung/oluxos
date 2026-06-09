#include <kernel/cpu.h>

CpuLocal cpus[MAX_CPUS];

void cpu_init(void) {
    int i;
    for (i = 0; i < MAX_CPUS; i++) {
        cpus[i].hartid = i;
        cpus[i].current_thread = 0;
        cpus[i].kernel_stack = 0;
        cpus[i].user_sp = 0;
        cpus[i].last_prev = 0;
    }
}
