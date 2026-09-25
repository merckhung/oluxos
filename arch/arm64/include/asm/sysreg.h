#ifndef ASM_SYSREG_H
#define ASM_SYSREG_H

#include <olux/const.h>

/* SCTLR_EL1 */
#define SCTLR_M (UL(1) << 0)
#define SCTLR_A (UL(1) << 1)
#define SCTLR_C (UL(1) << 2)
#define SCTLR_SA (UL(1) << 3)
#define SCTLR_SA0 (UL(1) << 4)
#define SCTLR_I (UL(1) << 12)
#define SCTLR_WXN (UL(1) << 19)
#define SCTLR_SPAN (UL(1) << 23)
#define SCTLR_EE (UL(1) << 25)
#define SCTLR_UCI (UL(1) << 26)
#define SCTLR_UCT (UL(1) << 15)
#define SCTLR_DZE (UL(1) << 14)
#define SCTLR_RES1 ((UL(1) << 11) | (UL(1) << 20) | (UL(1) << 22) | (UL(1) << 28) | (UL(1) << 29))
/* MMU + caches + alignment checks for SP; EL0 may use cache maintenance
 * (UCI), read CTR_EL0 (UCT) and DC ZVA (DZE), which musl relies on. */
#define SCTLR_EL1_MMU_ON \
  (SCTLR_RES1 | SCTLR_M | SCTLR_C | SCTLR_SA | SCTLR_SA0 | SCTLR_I | SCTLR_UCI | SCTLR_UCT | SCTLR_DZE)
#define SCTLR_EL1_MMU_OFF (SCTLR_RES1)

/* TCR_EL1 */
#define TCR_T0SZ(x) ((UL(64) - (x)) << 0)
#define TCR_T1SZ(x) ((UL(64) - (x)) << 16)
#define TCR_IRGN0_WBWA (UL(1) << 8)
#define TCR_ORGN0_WBWA (UL(1) << 10)
#define TCR_SH0_INNER (UL(3) << 12)
#define TCR_TG0_4K (UL(0) << 14)
#define TCR_EPD0 (UL(1) << 7)
#define TCR_IRGN1_WBWA (UL(1) << 24)
#define TCR_ORGN1_WBWA (UL(1) << 26)
#define TCR_SH1_INNER (UL(3) << 28)
#define TCR_TG1_4K (UL(2) << 30)
#define TCR_IPS_SHIFT 32
#define TCR_AS (UL(1) << 36)
#define TCR_TBI0 (UL(1) << 37)

/* MAIR attribute indices */
#define MT_DEVICE_nGnRnE 0
#define MT_NORMAL 1
#define MT_NORMAL_NC 2
#define MT_DEVICE_nGnRE 3
#define MAIR_VALUE ((UL(0x00) << (8 * MT_DEVICE_nGnRnE)) | (UL(0xff) << (8 * MT_NORMAL)) | \
                    (UL(0x44) << (8 * MT_NORMAL_NC)) | (UL(0x04) << (8 * MT_DEVICE_nGnRE)))

/* HCR_EL2 */
#define HCR_RW (UL(1) << 31)

/* SPSR */
#define PSR_MODE_EL0t 0x0
#define PSR_MODE_EL1t 0x4
#define PSR_MODE_EL1h 0x5
#define PSR_MODE_EL2h 0x9
#define PSR_MODE_MASK 0xf
#define PSR_F (UL(1) << 6)
#define PSR_I (UL(1) << 7)
#define PSR_A (UL(1) << 8)
#define PSR_D (UL(1) << 9)
#define PSR_DAIF (PSR_D | PSR_A | PSR_I | PSR_F)

/* ESR_EL1 exception classes */
#define ESR_EC_SHIFT 26
#define ESR_EC_UNKNOWN 0x00
#define ESR_EC_WFX 0x01
#define ESR_EC_FP_ASIMD 0x07
#define ESR_EC_ILL_STATE 0x0e
#define ESR_EC_SVC64 0x15
#define ESR_EC_SYS64 0x18
#define ESR_EC_IABT_LOW 0x20
#define ESR_EC_IABT_CUR 0x21
#define ESR_EC_PC_ALIGN 0x22
#define ESR_EC_DABT_LOW 0x24
#define ESR_EC_DABT_CUR 0x25
#define ESR_EC_SP_ALIGN 0x26
#define ESR_EC_FP_EXC64 0x2c
#define ESR_EC_SERROR 0x2f
#define ESR_EC_BREAKPT_LOW 0x30
#define ESR_EC_SOFTSTP_LOW 0x32
#define ESR_EC_WATCHPT_LOW 0x34
#define ESR_EC_BRK64 0x3c
#define ESR_ELx_WNR (UL(1) << 6)
#define ESR_ELx_FSC_MASK 0x3f

#ifndef __ASSEMBLY__
#define read_sysreg(r)                              \
  ({                                                \
    unsigned long __rv;                             \
    __asm__ volatile("mrs %0, " #r : "=r"(__rv));   \
    __rv;                                           \
  })
#define write_sysreg(r, v)                                        \
  do {                                                            \
    unsigned long __wv = (unsigned long)(v);                      \
    __asm__ volatile("msr " #r ", %0" : : "r"(__wv) : "memory");  \
  } while (0)
#define isb() __asm__ volatile("isb" ::: "memory")
#define dsb(opt) __asm__ volatile("dsb " #opt ::: "memory")
#define dmb(opt) __asm__ volatile("dmb " #opt ::: "memory")
#define wfi() __asm__ volatile("wfi" ::: "memory")
#define wfe() __asm__ volatile("wfe" ::: "memory")
#define sev() __asm__ volatile("sev" ::: "memory")

#define mb() dsb(sy)
#define rmb() dsb(ld)
#define wmb() dsb(st)
#define smp_mb() dmb(ish)
#define smp_rmb() dmb(ishld)
#define smp_wmb() dmb(ishst)
#endif

#endif
