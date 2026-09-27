#ifndef __KERNEL_INTERRUPT_H__
#define __KERNEL_INTERRUPT_H__

#if defined(CONFIG_ARCH_RISCV64)
#include <riscv64/interrupt.h>
#elif defined(CONFIG_ARCH_RISCV32)
#include <riscv32/interrupt.h>
#elif defined(CONFIG_ARCH_ARM64)
#include <arm64/interrupt.h>
#elif defined(CONFIG_ARCH_IA32)
void IntEnable(void);
void IntDisable(void);
#else
#error "Unsupported architecture for interrupt.h"
#endif

#endif // __KERNEL_INTERRUPT_H__
