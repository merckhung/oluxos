/*
 * BCM2711 PCIe root complex (Raspberry Pi 4), which hosts the VL805 USB 3
 * controller. Brings the link up and sets the outbound (CPU -> PCIe) and
 * inbound (PCIe -> memory) windows; configuration space of the root port
 * is the bridge's own register block, devices behind it are reached
 * through an index/data window. Legacy INTx interrupts are used.
 *
 * After the fundamental reset the VL805 has lost its firmware: the VPU
 * reloads it when told through the mailbox, before xHCI starts.
 *
 * Sequence follows Linux's pcie-brcmstb driver. QEMU's raspi4b does not
 * model this block, so it is exercised only on real boards.
 */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/pci.h>
#include <olux/rpi_firmware.h>
#include <olux/sched.h>
#include <olux/time.h>

#define RC_CFG_VENDOR_SPECIFIC_REG1 0x0188
#define RC_CFG_PRIV1_ID_VAL3 0x043c
#define MISC_MISC_CTRL 0x4008
#define MISC_CTRL_SCB_ACCESS_EN (1u << 12)
#define MISC_CTRL_CFG_READ_UR_MODE (1u << 13)
#define MISC_CTRL_MAX_BURST_MASK (3u << 20)
#define MISC_CTRL_SCB0_SIZE_SHIFT 27
#define MISC_CPU_2_PCIE_MEM_WIN0_LO 0x400c
#define MISC_CPU_2_PCIE_MEM_WIN0_HI 0x4010
#define MISC_RC_BAR1_CONFIG_LO 0x402c
#define MISC_RC_BAR2_CONFIG_LO 0x4034
#define MISC_RC_BAR2_CONFIG_HI 0x4038
#define MISC_RC_BAR3_CONFIG_LO 0x403c
#define MISC_PCIE_STATUS 0x4068
#define STATUS_PHYLINKUP (1u << 4)
#define STATUS_DL_ACTIVE (1u << 5)
#define STATUS_PORT_RC (1u << 7)
#define MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT 0x4070
#define MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI 0x4080
#define MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI 0x4084
#define MISC_HARD_PCIE_HARD_DEBUG 0x4204
#define HARD_DEBUG_CLKREQ_DEBUG_ENABLE (1u << 1)
#define HARD_DEBUG_SERDES_IDDQ (1u << 27)
#define EXT_CFG_DATA 0x8000
#define EXT_CFG_INDEX 0x9000
#define RGR1_SW_INIT_1 0x9210
#define SW_INIT_PERST 1u
#define SW_INIT_BRIDGE 2u

#define RPI_FW_NOTIFY_XHCI_RESET 0x00030058

struct brcm_pcie {
  u8 *base;
};

static void *brcm_cfg(struct pci_host *h, u8 bus, u8 dev, u8 fn, unsigned off) {
  struct brcm_pcie *p = h->priv;
  if (bus == 0) return dev || fn ? NULL : p->base + (off & 0xffc); /* the root port itself */
  if (dev != 0) return NULL;                                        /* one link, one device */
  writel((u32)bus << 20 | (u32)dev << 15 | (u32)fn << 12, p->base + EXT_CFG_INDEX);
  return p->base + EXT_CFG_DATA + (off & 0xffc);
}

static u32 rd(struct brcm_pcie *p, u32 off) { return readl(p->base + off); }
static void wr(struct brcm_pcie *p, u32 off, u32 v) { writel(v, p->base + off); }

/* Inbound window size encoding (RC_BAR2_CONFIG_LO.SIZE). */
static u32 encode_ibar_size(u64 size) {
  int log2 = 63 - __builtin_clzll(size);
  if (log2 >= 12 && log2 <= 15) return (u32)(log2 - 12) + 0x1c;
  if (log2 >= 16 && log2 <= 35) return (u32)(log2 - 15);
  return 0;
}

static void set_outbound(struct brcm_pcie *p, u64 cpu, u64 pci, u64 size) {
  wr(p, MISC_CPU_2_PCIE_MEM_WIN0_LO, (u32)pci);
  wr(p, MISC_CPU_2_PCIE_MEM_WIN0_HI, (u32)(pci >> 32));
  u64 base_mb = cpu >> 20, limit_mb = (cpu + size - 1) >> 20;
  u32 v = rd(p, MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT);
  v = (v & ~0xfff0u) | (u32)(base_mb & 0xfff) << 4;
  v = (v & ~0xfff00000u) | (u32)(limit_mb & 0xfff) << 20;
  wr(p, MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT, v);
  wr(p, MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI, (rd(p, MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI) & ~0xffu) | (u32)(base_mb >> 12));
  wr(p, MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI, (rd(p, MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI) & ~0xffu) | (u32)(limit_mb >> 12));
}

static int brcm_pcie_probe(int node) {
  struct brcm_pcie *p = kzalloc(sizeof(*p), 0);
  struct pci_host *h = kzalloc(sizeof(*h), 0);
  if (!p || !h) return -ENOMEM;
  p->base = dt_ioremap(node, 0, NULL);
  if (!p->base) return -ENOMEM;
  h->node = node;
  h->cfg = brcm_cfg;
  h->priv = p;
  h->root_port_no_bars = true;
  pci_host_parse_ranges(h);
  if (!h->mem_size) return -EINVAL;

  /* reset the bridge and assert PERST# */
  wr(p, RGR1_SW_INIT_1, rd(p, RGR1_SW_INIT_1) | SW_INIT_BRIDGE | SW_INIT_PERST);
  udelay(200);
  wr(p, RGR1_SW_INIT_1, rd(p, RGR1_SW_INIT_1) & ~SW_INIT_BRIDGE);
  wr(p, MISC_HARD_PCIE_HARD_DEBUG, rd(p, MISC_HARD_PCIE_HARD_DEBUG) & ~HARD_DEBUG_SERDES_IDDQ);
  udelay(200);

  /* inbound: PCIe addresses map to memory from dma-ranges (rounded up to a power of two) */
  u64 inbound = 0;
  int len;
  const u32 *dr = fdt_getprop(node, "dma-ranges", &len);
  if (dr && len >= 7 * 4) inbound = fdt_read_cells(&dr[5], 2);
  if (!inbound) inbound = 1ULL << 32;
  u64 bar2 = 1ULL << (64 - __builtin_clzll(inbound - 1));
  u32 ctrl = rd(p, MISC_MISC_CTRL);
  ctrl |= MISC_CTRL_SCB_ACCESS_EN | MISC_CTRL_CFG_READ_UR_MODE;
  ctrl &= ~MISC_CTRL_MAX_BURST_MASK; /* 128-byte bursts on BCM2711 */
  ctrl = (ctrl & ~(0x1fu << MISC_CTRL_SCB0_SIZE_SHIFT)) | (u32)(63 - __builtin_clzll(bar2) - 15) << MISC_CTRL_SCB0_SIZE_SHIFT;
  wr(p, MISC_MISC_CTRL, ctrl);
  wr(p, MISC_RC_BAR2_CONFIG_LO, encode_ibar_size(bar2)); /* offset 0 */
  wr(p, MISC_RC_BAR2_CONFIG_HI, 0);
  wr(p, MISC_RC_BAR1_CONFIG_LO, rd(p, MISC_RC_BAR1_CONFIG_LO) & ~0x1fu); /* no GISB / SCB windows */
  wr(p, MISC_RC_BAR3_CONFIG_LO, rd(p, MISC_RC_BAR3_CONFIG_LO) & ~0x1fu);
  wr(p, RC_CFG_PRIV1_ID_VAL3, (rd(p, RC_CFG_PRIV1_ID_VAL3) & ~0xffffffu) | 0x060400); /* PCI bridge class */
  wr(p, RC_CFG_VENDOR_SPECIFIC_REG1, rd(p, RC_CFG_VENDOR_SPECIFIC_REG1) & ~0xcu); /* little-endian BAR2 */
  set_outbound(p, h->mem_cpu, h->mem_pci, h->mem_size);

  /* release PERST# and wait for the link */
  wr(p, RGR1_SW_INIT_1, rd(p, RGR1_SW_INIT_1) & ~SW_INIT_PERST);
  sleep_ns(100 * 1000000);
  u32 st = 0;
  for (int i = 0; i < 20; i++) {
    st = rd(p, MISC_PCIE_STATUS);
    if ((st & STATUS_PHYLINKUP) && (st & STATUS_DL_ACTIVE)) break;
    sleep_ns(5 * 1000000);
  }
  if (!(st & STATUS_PHYLINKUP) || !(st & STATUS_DL_ACTIVE)) {
    pr_warn("pcie-brcmstb: link down\n");
    return -ENODEV;
  }
  if (!(st & STATUS_PORT_RC)) {
    pr_warn("pcie-brcmstb: controller is not in root-complex mode\n");
    return -ENODEV;
  }
  wr(p, MISC_HARD_PCIE_HARD_DEBUG, rd(p, MISC_HARD_PCIE_HARD_DEBUG) | HARD_DEBUG_CLKREQ_DEBUG_ENABLE);
  pr_info("pcie-brcmstb: link up, outbound %#llx -> pci %#llx (%llu MiB), inbound %llu MiB\n",
          (unsigned long long)h->mem_cpu, (unsigned long long)h->mem_pci, (unsigned long long)(h->mem_size >> 20),
          (unsigned long long)(inbound >> 20));

  /* the VL805 needs its firmware reloaded after PERST# (device 01:00.0) */
  if (rpi_fw_available()) {
    u32 addr = 1u << 20;
    if (rpi_fw_property(RPI_FW_NOTIFY_XHCI_RESET, &addr, 4, 4) < 0)
      pr_warn("pcie-brcmstb: VL805 firmware reload request failed\n");
    sleep_ns(10 * 1000000);
  }
  return pci_host_scan(h);
}

DT_DRIVER(brcm_pcie, DRV_DEVICE, brcm_pcie_probe, "brcm,bcm2711-pcie");
