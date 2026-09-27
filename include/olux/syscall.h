#ifndef OLUX_SYSCALL_H
#define OLUX_SYSCALL_H

#include <asm/ptrace.h>
#include <olux/types.h>

typedef long (*syscall_fn_t)(u64, u64, u64, u64, u64, u64);

#define NR_SYSCALLS 512
/* OluxOS-specific system calls live above the Linux number space. */
#define OLUX_SYSCALL_BASE 1000
#define NR_OLUX_SYSCALLS 16

void do_syscall(struct pt_regs *regs);
const char *syscall_name(long nr);

#endif
