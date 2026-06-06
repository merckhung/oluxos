#include <types.h>
#include <driver/gicv2.h>

#define GICD_BASE 0x08000000ULL
#define GICC_BASE 0x08010000ULL

#define GICD_CTLR          ((volatile u32 *)(GICD_BASE + 0x000))
#define GICD_TYPER         ((volatile u32 *)(GICD_BASE + 0x004))
#define GICD_ISENABLER(n)  ((volatile u32 *)(GICD_BASE + 0x100 + (n) * 4))
#define GICD_ICENABLER(n)  ((volatile u32 *)(GICD_BASE + 0x180 + (n) * 4))
#define GICD_IPRIORITYR(n) ((volatile u32 *)(GICD_BASE + 0x400 + (n) * 4))
#define GICD_ITARGETSR(n)  ((volatile u32 *)(GICD_BASE + 0x800 + (n) * 4))
#define GICD_ICFGR(n)      ((volatile u32 *)(GICD_BASE + 0xC00 + (n) * 4))

#define GICC_CTLR          ((volatile u32 *)(GICC_BASE + 0x000))
#define GICC_PMR           ((volatile u32 *)(GICC_BASE + 0x004))
#define GICC_IAR           ((volatile u32 *)(GICC_BASE + 0x00C))
#define GICC_EOIR          ((volatile u32 *)(GICC_BASE + 0x010))

void gicv2_init(void) {
    // 1. Disable Distributor
    *GICD_CTLR = 0;

    // 2. Disable CPU Interface
    *GICC_CTLR = 0;

    // 3. Set Priority Mask to allow all interrupts (lowest priority is 0xFF)
    *GICC_PMR = 0xFF;

    // 4. Enable CPU Interface (Group 0)
    *GICC_CTLR = 1;

    // 5. Enable Distributor (Group 0)
    *GICD_CTLR = 1;
}

void gicv2_enable_irq(u32 irq) {
    u32 reg_idx = irq / 32;
    u32 bit_idx = irq % 32;
    
    // Enable interrupt
    *GICD_ISENABLER(reg_idx) = (1 << bit_idx);
}

void gicv2_disable_irq(u32 irq) {
    u32 reg_idx = irq / 32;
    u32 bit_idx = irq % 32;
    
    // Disable interrupt
    *GICD_ICENABLER(reg_idx) = (1 << bit_idx);
}

void gicv2_set_irq_priority(u32 irq, u8 priority) {
    u32 reg_idx = irq / 4;
    u32 byte_idx = irq % 4;
    
    u32 val = *GICD_IPRIORITYR(reg_idx);
    val &= ~(0xFF << (byte_idx * 8));
    val |= (priority << (byte_idx * 8));
    *GICD_IPRIORITYR(reg_idx) = val;
}

u32 gicv2_acknowledge_irq(void) {
    return *GICC_IAR & 0x3FF; // Bits 9:0 contain INTID
}

void gicv2_end_of_irq(u32 irq) {
    *GICC_EOIR = irq;
}
