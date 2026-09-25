/*
 * ARM Generic Interrupt Controller: GICv2 (GIC-400 on BCM2711, QEMU virt
 * default) and GICv3 (QEMU virt,gic-version=3). All interrupts are
 * configured as non-secure Group 1 and delivered as IRQs.
 */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/smp.h>

#define GICD_CTLR 0x000
#define GICD_TYPER 0x004
#define GICD_IGROUPR 0x080
#define GICD_ISENABLER 0x100
#define GICD_ICENABLER 0x180
#define GICD_ICPENDR 0x280
#define GICD_ICACTIVER 0x380
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR 0x800
#define GICD_ICFGR 0xc00
#define GICD_SGIR 0xf00
#define GICD_IROUTER 0x6000 /* + 8 * INTID */
#define GICD_CTLR_RWP (1U << 31)

#define GICC_CTLR 0x00
#define GICC_PMR 0x04
#define GICC_BPR 0x08
#define GICC_IAR 0x0c
#define GICC_EOIR 0x10

#define GICR_CTLR 0x0000
#define GICR_TYPER 0x0008
#define GICR_WAKER 0x0014
#define GICR_SGI_BASE 0x10000
#define GICR_STRIDE 0x20000

#define DEFAULT_PRIO 0xa0

static u8 *gicd, *gicc, *gicr_base;
static u64 gicr_size;
static int nr_irqs;
static bool v3;
static u8 cpu_gic_mask[NR_CPUS];  /* v2: CPU interface bit per logical CPU */
static u8 *cpu_gicr[NR_CPUS];     /* v3: redistributor per logical CPU */
static u32 last_iar[NR_CPUS];
static DEFINE_SPINLOCK(gic_lock);

#define ICC_IAR1_EL1 s3_0_c12_c12_0
#define ICC_EOIR1_EL1 s3_0_c12_c12_1
#define ICC_PMR_EL1 s3_0_c4_c6_0
#define ICC_BPR1_EL1 s3_0_c12_c12_3
#define ICC_CTLR_EL1 s3_0_c12_c12_4
#define ICC_SRE_EL1 s3_0_c12_c12_5
#define ICC_IGRPEN1_EL1 s3_0_c12_c12_7
#define ICC_SGI1R_EL1 s3_0_c12_c11_5

static u8 *sgi_frame(void) { return cpu_gicr[smp_processor_id()] + GICR_SGI_BASE; }

static void wait_rwp(void) {
  for (int i = 0; i < 1000000 && (readl(gicd + GICD_CTLR) & GICD_CTLR_RWP); i++)
    ;
}

static void gic_enable(int irq) {
  u32 bit = 1U << (irq % 32);
  if (v3 && irq < 32) writel(bit, sgi_frame() + GICD_ISENABLER);
  else writel(bit, gicd + GICD_ISENABLER + (irq / 32) * 4);
}

static void gic_disable(int irq) {
  u32 bit = 1U << (irq % 32);
  if (v3 && irq < 32) writel(bit, sgi_frame() + GICD_ICENABLER);
  else writel(bit, gicd + GICD_ICENABLER + (irq / 32) * 4);
  if (v3) wait_rwp();
}

static void gic_set_type(int irq, unsigned type) {
  if (irq < 16) return;
  u8 *base = (v3 && irq < 32) ? sgi_frame() : gicd;
  unsigned long f = spin_lock_irqsave(&gic_lock);
  u32 off = GICD_ICFGR + (irq / 16) * 4, shift = (irq % 16) * 2;
  u32 v = readl(base + off);
  if (type & IRQ_TYPE_EDGE_RISING) v |= 2U << shift;
  else v &= ~(2U << shift);
  writel(v, base + off);
  spin_unlock_irqrestore(&gic_lock, f);
}

static void gic_set_affinity(int irq, int cpu) {
  if (irq < 32) return;
  if (v3) {
    u64 mpidr = cpus[cpu].mpidr;
    u64 aff = (mpidr & 0xffffff) | ((mpidr >> 32 & 0xff) << 32);
    writeq(aff, gicd + GICD_IROUTER + irq * 8);
  } else {
    writeb(cpu_gic_mask[cpu], gicd + GICD_ITARGETSR + irq);
  }
}

static int gic_ack(void) {
  u32 iar;
  if (v3) {
    iar = read_sysreg(ICC_IAR1_EL1);
    isb();
  } else {
    iar = readl(gicc + GICC_IAR);
  }
  u32 irq = iar & (v3 ? 0xffffff : 0x3ff);
  if (irq >= 1020 && irq <= 1023) return -1;
  last_iar[smp_processor_id()] = iar;
  return irq;
}

static void gic_eoi(int irq) {
  if (v3) {
    write_sysreg(ICC_EOIR1_EL1, irq);
    isb();
  } else {
    writel(last_iar[smp_processor_id()], gicc + GICC_EOIR);
  }
}

static void gic_send_sgi(int sgi, int cpu) {
  dsb(ishst);
  if (v3) {
    u64 m = cpus[cpu].mpidr;
    u64 v = ((m >> 32 & 0xff) << 48) | ((m >> 16 & 0xff) << 32) | ((m >> 8 & 0xff) << 16) |
            ((u64)sgi << 24) | (1UL << (m & 0xf));
    write_sysreg(ICC_SGI1R_EL1, v);
    isb();
  } else {
    writel(((u32)cpu_gic_mask[cpu] << 16) | sgi, gicd + GICD_SGIR);
  }
}

static void gic_cpu_init(void) {
  int cpu = smp_processor_id();
  if (v3) {
    /* find this CPU's redistributor by affinity */
    u64 mpidr = read_sysreg(mpidr_el1);
    u32 aff = ((mpidr >> 32 & 0xff) << 24) | (mpidr & 0xffffff);
    for (u64 off = 0; off + GICR_STRIDE <= gicr_size; off += GICR_STRIDE) {
      u64 typer = readq(gicr_base + off + GICR_TYPER);
      if ((u32)(typer >> 32) == aff) {
        cpu_gicr[cpu] = gicr_base + off;
        break;
      }
      if (typer & (1UL << 4)) break; /* Last */
    }
    if (!cpu_gicr[cpu]) panic("gicv3: no redistributor for CPU %d", cpu);
    u8 *rd = cpu_gicr[cpu];
    writel(readl(rd + GICR_WAKER) & ~2U, rd + GICR_WAKER);
    for (int i = 0; i < 1000000 && (readl(rd + GICR_WAKER) & 4); i++)
      ;
    u8 *sgi = rd + GICR_SGI_BASE;
    writel(0xffffffff, sgi + GICD_IGROUPR);
    writel(0xffff0000, sgi + GICD_ICENABLER);
    writel(0xffffffff, sgi + GICD_ICPENDR);
    for (int i = 0; i < 32; i += 4) writel(0xa0a0a0a0, sgi + GICD_IPRIORITYR + i);
    writel(0x0000ffff, sgi + GICD_ISENABLER);
    write_sysreg(ICC_SRE_EL1, read_sysreg(ICC_SRE_EL1) | 1);
    isb();
    write_sysreg(ICC_PMR_EL1, 0xf0);
    write_sysreg(ICC_BPR1_EL1, 0);
    write_sysreg(ICC_CTLR_EL1, 0);
    write_sysreg(ICC_IGRPEN1_EL1, 1);
    isb();
  } else {
    /* Interrupt groups are left as configured by firmware/reset: on a GIC
     * without security extensions (QEMU) Group 0 is acknowledged through
     * GICC_IAR; behind TF-A/armstub (Pi 4) everything is non-secure Group 1. */
    cpu_gic_mask[cpu] = readl(gicd + GICD_ITARGETSR) & 0xff;
    writel(0xffff0000, gicd + GICD_ICENABLER);
    writel(0x0000ffff, gicd + GICD_ISENABLER);
    for (int i = 0; i < 32; i += 4) writel(0xa0a0a0a0, gicd + GICD_IPRIORITYR + i);
    writel(0xf0, gicc + GICC_PMR);
    writel(0, gicc + GICC_BPR);
    writel(3, gicc + GICC_CTLR); /* enable Group 0 and Group 1 */
  }
}

static const struct irq_chip gic_chip = {
    .name = "GIC",
    .enable = gic_enable,
    .disable = gic_disable,
    .set_type = gic_set_type,
    .set_affinity = gic_set_affinity,
    .send_sgi = gic_send_sgi,
    .ack = gic_ack,
    .eoi = gic_eoi,
    .cpu_init = gic_cpu_init,
};

static void dist_init(void) {
  writel(0, gicd + GICD_CTLR);
  if (v3) wait_rwp();
  nr_irqs = MIN(32 * ((readl(gicd + GICD_TYPER) & 0x1f) + 1), 1020);
  for (int i = 32; i < nr_irqs; i += 32) {
    if (v3) writel(0xffffffff, gicd + GICD_IGROUPR + i / 8);
    writel(0xffffffff, gicd + GICD_ICENABLER + i / 8);
    writel(0xffffffff, gicd + GICD_ICPENDR + i / 8);
    writel(0xffffffff, gicd + GICD_ICACTIVER + i / 8);
  }
  for (int i = 32; i < nr_irqs; i += 16) writel(0, gicd + GICD_ICFGR + i / 4); /* level */
  for (int i = 32; i < nr_irqs; i += 4) writel(0xa0a0a0a0, gicd + GICD_IPRIORITYR + i);
  if (v3) {
    u64 m = read_sysreg(mpidr_el1);
    u64 aff = (m & 0xffffff) | ((m >> 32 & 0xff) << 32);
    for (int i = 32; i < nr_irqs; i++) writeq(aff, gicd + GICD_IROUTER + i * 8);
    writel((1U << 4) | (1U << 1), gicd + GICD_CTLR); /* ARE, EnableGrp1 */
    wait_rwp();
  } else {
    u32 me = readl(gicd + GICD_ITARGETSR) & 0xff;
    u32 t = me | me << 8 | me << 16 | me << 24;
    for (int i = 32; i < nr_irqs; i += 4) writel(t, gicd + GICD_ITARGETSR + i);
    writel(3, gicd + GICD_CTLR);
  }
}

static int gic_probe(int node) {
  v3 = fdt_is_compatible(node, "arm,gic-v3");
  gicd = dt_ioremap(node, 0, NULL);
  if (v3) {
    u64 addr;
    if (fdt_get_reg(node, 1, &addr, &gicr_size)) return -EINVAL;
    gicr_base = ioremap(addr, gicr_size);
  } else {
    gicc = dt_ioremap(node, 1, NULL);
  }
  if (!gicd || (!gicc && !gicr_base)) return -ENOMEM;
  dist_init();
  irq_set_chip(&gic_chip);
  gic_cpu_init();
  pr_info("GICv%d: %d interrupts\n", v3 ? 3 : 2, nr_irqs);
  return 0;
}

DT_DRIVER(gic, DRV_IRQCHIP, gic_probe, "arm,gic-400", "arm,cortex-a15-gic", "arm,cortex-a9-gic",
          "arm,gic-v3");
