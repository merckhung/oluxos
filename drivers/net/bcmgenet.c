/*
 * Broadcom GENET v5 Ethernet MAC (Raspberry Pi 4 / BCM2711) with its
 * RGMII PHY (BCM54213PE) on the UniMAC MDIO bus.
 *
 * Only the default descriptor ring (16) is used, with all 256 receive and
 * 256 transmit descriptors. Buffers are uncached DMA memory, so frames are
 * copied in and out without cache maintenance. The PHY link is polled once
 * a second and the MAC speed/duplex follow it.
 *
 * The register sequence follows Linux's bcmgenet driver. QEMU does not
 * model GENET, so this driver is exercised only on real boards.
 */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/netdev.h>
#include <olux/rpi_firmware.h>
#include <olux/time.h>

/* register blocks */
#define SYS_OFF 0x0000
#define EXT_OFF 0x0080
#define INTRL2_0_OFF 0x0200
#define RBUF_OFF 0x0300
#define TBUF_OFF 0x0600
#define UMAC_OFF 0x0800
#define RDMA_OFF 0x2000
#define TDMA_OFF 0x4000
#define HFB_REG_OFF 0xfc00

#define SYS_PORT_CTRL 0x04
#define PORT_MODE_EXT_GPHY 3
#define SYS_RBUF_FLUSH_CTRL 0x08

#define EXT_RGMII_OOB_CTRL 0x0c
#define RGMII_LINK (1u << 4)
#define OOB_DISABLE (1u << 5)
#define RGMII_MODE_EN (1u << 6)
#define ID_MODE_DIS (1u << 16)

#define INTRL2_STAT 0x00
#define INTRL2_CLEAR 0x08
#define INTRL2_MASK_STATUS 0x0c
#define INTRL2_MASK_SET 0x10
#define INTRL2_MASK_CLEAR 0x14
#define IRQ_RXDMA_DONE (1u << 13)
#define IRQ_TXDMA_DONE (1u << 16)

#define RBUF_CTRL 0x00
#define RBUF_64B_EN (1u << 0)
#define RBUF_ALIGN_2B (1u << 1)
#define RBUF_TBUF_SIZE_CTRL 0xb4
#define TBUF_CTRL 0x00

#define UMAC_CMD 0x008
#define CMD_TX_EN (1u << 0)
#define CMD_RX_EN (1u << 1)
#define CMD_SPEED_SHIFT 2
#define CMD_SPEED_MASK 3u
#define CMD_PROMISC (1u << 4)
#define CMD_CRC_FWD (1u << 6)
#define CMD_RX_PAUSE_IGNORE (1u << 8)
#define CMD_HD_EN (1u << 10)
#define CMD_SW_RESET (1u << 13)
#define CMD_TX_PAUSE_IGNORE (1u << 28)
#define UMAC_MAC0 0x00c
#define UMAC_MAC1 0x010
#define UMAC_MAX_FRAME_LEN 0x014
#define UMAC_TX_FLUSH 0x334
#define UMAC_MIB_CTRL 0x580
#define UMAC_MDIO_CMD 0x614
#define MDIO_START_BUSY (1u << 29)
#define MDIO_READ_FAIL (1u << 28)
#define MDIO_RD (2u << 26)
#define MDIO_WR (1u << 26)
#define UMAC_MDF_CTRL 0x650
#define UMAC_MDF_ADDR 0x654
#define MAX_MDF 17

/* DMA: descriptors (3 words each) then 17 rings of 0x40, then control */
#define TOTAL_DESC 256
#define DESC_SIZE 12
#define DESC_INDEX 16
#define RING_SIZE 0x40
#define RING_REGS(dma) ((dma) + TOTAL_DESC * DESC_SIZE + RING_SIZE * DESC_INDEX)
#define DMA_REGS(dma) ((dma) + TOTAL_DESC * DESC_SIZE + RING_SIZE * (DESC_INDEX + 1))
/* ring registers (RDMA names; TDMA swaps read/write and prod/cons) */
#define R_WRITE_PTR 0x00
#define R_PROD_INDEX 0x08
#define R_CONS_INDEX 0x0c
#define R_BUF_SIZE 0x10
#define R_START_ADDR 0x14
#define R_END_ADDR 0x1c
#define R_MBUF_DONE_THRESH 0x24
#define R_XON_XOFF 0x28
#define R_READ_PTR 0x2c
#define T_READ_PTR 0x00
#define T_CONS_INDEX 0x08
#define T_PROD_INDEX 0x0c
#define T_FLOW_PERIOD 0x28
#define T_WRITE_PTR 0x2c
/* control registers */
#define DMA_RING_CFG 0x00
#define DMA_CTRL 0x04
#define DMA_SCB_BURST_SIZE 0x0c
#define DMA_EN 1u
#define DMA_RING16_EN (1u << (DESC_INDEX + 1))

/* descriptor fields */
#define DESC_LEN_STATUS 0x00
#define DESC_ADDR_LO 0x04
#define DESC_ADDR_HI 0x08
#define DMA_BUFLENGTH_SHIFT 16
#define DMA_EOP 0x4000
#define DMA_SOP 0x2000
#define DMA_TX_APPEND_CRC 0x0040
#define DMA_TX_QTAG_SHIFT 7
#define DMA_RX_ERRORS 0x001f /* overrun, CRC, RX error, no-good, too long */
#define QTAG_MASK 0x3f

#define BUF_LEN 2048
#define MAX_FRAME 1536

/* PHY registers */
#define MII_BMCR 0x00
#define MII_BMSR 0x01
#define MII_PHYID1 0x02
#define MII_ADVERTISE 0x04
#define MII_LPA 0x05
#define MII_CTRL1000 0x09
#define MII_STAT1000 0x0a
#define MII_BCM54XX_SHD 0x1c
#define MII_BCM54XX_AUX_CTL 0x18
#define BMCR_RESET 0x8000
#define BMCR_ANENABLE 0x1000
#define BMCR_ANRESTART 0x0200
#define BMSR_LSTATUS 0x0004
#define BMSR_ANEGCOMPLETE 0x0020

struct genet {
  u8 *base;
  int node;
  int phy;
  u8 *rxbuf, *txbuf;
  phys_addr_t rxpa, txpa;
  u16 rx_c_index;
  u16 tx_prod, tx_cons;
  bool crc_fwd;
  bool link;
  int speed;
  bool full_duplex;
  volatile bool check_link;
  struct ktimer link_timer;
  struct net_device nd;
};

static u32 rd(struct genet *g, u32 off) { return readl(g->base + off); }
static void wr(struct genet *g, u32 off, u32 v) { writel(v, g->base + off); }

/* ---------------- MDIO / PHY ---------------- */

static int mdio_wait(struct genet *g) {
  for (int i = 0; i < 1000; i++) {
    if (!(rd(g, UMAC_OFF + UMAC_MDIO_CMD) & MDIO_START_BUSY)) return 0;
    udelay(10);
  }
  return -ETIMEDOUT;
}

static int mdio_read(struct genet *g, int reg) {
  wr(g, UMAC_OFF + UMAC_MDIO_CMD, MDIO_RD | (u32)g->phy << 21 | (u32)reg << 16);
  wr(g, UMAC_OFF + UMAC_MDIO_CMD, rd(g, UMAC_OFF + UMAC_MDIO_CMD) | MDIO_START_BUSY);
  if (mdio_wait(g)) return -ETIMEDOUT;
  u32 v = rd(g, UMAC_OFF + UMAC_MDIO_CMD);
  return (v & MDIO_READ_FAIL) ? -EIO : (int)(v & 0xffff);
}

static int mdio_write(struct genet *g, int reg, u16 val) {
  wr(g, UMAC_OFF + UMAC_MDIO_CMD, MDIO_WR | (u32)g->phy << 21 | (u32)reg << 16 | val);
  wr(g, UMAC_OFF + UMAC_MDIO_CMD, rd(g, UMAC_OFF + UMAC_MDIO_CMD) | MDIO_START_BUSY);
  return mdio_wait(g);
}

/* BCM54xx: RGMII receive clock skew in the PHY (phy-mode "rgmii-rxid"),
 * no transmit clock delay in the PHY (the MAC adds it). */
static void phy_rgmii_delays(struct genet *g, bool rx_skew, bool tx_delay) {
  mdio_write(g, MII_BCM54XX_AUX_CTL, 0x0007 | 0x7 << 12); /* select shadow 7 (misc) for reading */
  int misc = mdio_read(g, MII_BCM54XX_AUX_CTL);
  if (misc >= 0) {
    misc |= 0x8000; /* write enable */
    misc = rx_skew ? misc | 0x0100 : misc & ~0x0100;
    mdio_write(g, MII_BCM54XX_AUX_CTL, (u16)((misc & ~7) | 0x7));
  }
  mdio_write(g, MII_BCM54XX_SHD, 0x3 << 10); /* shadow 0x03: clock alignment control */
  int clk = mdio_read(g, MII_BCM54XX_SHD);
  if (clk >= 0) {
    clk &= 0x3ff;
    clk = tx_delay ? clk | (1 << 9) : clk & ~(1 << 9);
    mdio_write(g, MII_BCM54XX_SHD, (u16)(0x8000 | 0x3 << 10 | clk));
  }
}

static int phy_init(struct genet *g) {
  int id1 = mdio_read(g, MII_PHYID1);
  if (id1 < 0 || id1 == 0xffff || id1 == 0) {
    /* not where the device tree says: scan the bus */
    for (g->phy = 0; g->phy < 32; g->phy++) {
      id1 = mdio_read(g, MII_PHYID1);
      if (id1 > 0 && id1 != 0xffff) break;
    }
    if (g->phy == 32) return -ENODEV;
  }
  mdio_write(g, MII_BMCR, BMCR_RESET);
  for (int i = 0; i < 100 && (mdio_read(g, MII_BMCR) & BMCR_RESET); i++) udelay(1000);
  const char *mode = fdt_getprop_str(g->node, "phy-mode");
  bool rxid = mode && (!strcmp(mode, "rgmii-rxid") || !strcmp(mode, "rgmii-id"));
  bool txid = mode && (!strcmp(mode, "rgmii-txid") || !strcmp(mode, "rgmii-id"));
  phy_rgmii_delays(g, rxid, txid);
  mdio_write(g, MII_ADVERTISE, 0x0de1); /* 10/100 half/full, pause, asym pause, CSMA */
  mdio_write(g, MII_CTRL1000, 0x0200);  /* 1000BASE-T full duplex */
  mdio_write(g, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
  pr_info("%s: PHY %#x at MDIO address %d (%s)\n", g->nd.name[0] ? g->nd.name : "genet", id1, g->phy,
          mode ? mode : "rgmii");
  return 0;
}

/* Program the MAC for the negotiated link. Stack lock held. */
static void update_link(struct genet *g) {
  int bmsr = mdio_read(g, MII_BMSR);
  bmsr = mdio_read(g, MII_BMSR); /* link status is latched low */
  bool up = bmsr >= 0 && (bmsr & BMSR_LSTATUS) && (bmsr & BMSR_ANEGCOMPLETE);
  if (up == g->link) return;
  g->link = up;
  if (up) {
    int lpa = mdio_read(g, MII_LPA), adv = mdio_read(g, MII_ADVERTISE);
    int stat1000 = mdio_read(g, MII_STAT1000), ctrl1000 = mdio_read(g, MII_CTRL1000);
    int common = lpa & adv;
    u32 speed = 0;
    g->full_duplex = true;
    if ((ctrl1000 & 0x0200) && (stat1000 & 0x0800)) {
      speed = 2, g->speed = 1000;
    } else if (common & 0x0100) {
      speed = 1, g->speed = 100;
    } else if (common & 0x0080) {
      speed = 1, g->speed = 100, g->full_duplex = false;
    } else if (common & 0x0040) {
      speed = 0, g->speed = 10;
    } else {
      speed = 0, g->speed = 10, g->full_duplex = false;
    }
    u32 oob = rd(g, EXT_OFF + EXT_RGMII_OOB_CTRL);
    wr(g, EXT_OFF + EXT_RGMII_OOB_CTRL, (oob & ~OOB_DISABLE) | RGMII_LINK);
    u32 cmd = rd(g, UMAC_OFF + UMAC_CMD);
    cmd &= ~(CMD_SPEED_MASK << CMD_SPEED_SHIFT | CMD_HD_EN | CMD_RX_PAUSE_IGNORE | CMD_TX_PAUSE_IGNORE);
    cmd |= speed << CMD_SPEED_SHIFT | (g->full_duplex ? 0 : CMD_HD_EN);
    if (!(common & 0x0400)) cmd |= CMD_RX_PAUSE_IGNORE | CMD_TX_PAUSE_IGNORE; /* no pause frames */
    wr(g, UMAC_OFF + UMAC_CMD, cmd | CMD_TX_EN | CMD_RX_EN);
    pr_info("%s: link up, %d Mb/s %s duplex\n", g->nd.name, g->speed, g->full_duplex ? "full" : "half");
  } else {
    pr_info("%s: link down\n", g->nd.name);
  }
  g->nd.link_up = up;
}

/* ---------------- data path ---------------- */

static u8 *desc(struct genet *g, u32 dma, int i) { return g->base + dma + i * DESC_SIZE; }

static void set_desc_addr(struct genet *g, u32 dma, int i, u64 bus) {
  writel((u32)bus, desc(g, dma, i) + DESC_ADDR_LO);
  writel((u32)(bus >> 32), desc(g, dma, i) + DESC_ADDR_HI);
}

static int genet_poll(struct net_device *nd, int budget) {
  struct genet *g = nd->priv;
  if (g->check_link) {
    g->check_link = false;
    bool was = g->link;
    update_link(g);
    if (was != g->link) netdev_link_changed_locked(nd);
  }
  u32 p = rd(g, RING_REGS(RDMA_OFF) + R_PROD_INDEX) & 0xffff;
  int n = 0;
  while (n < budget && g->rx_c_index != p) {
    int i = g->rx_c_index % TOTAL_DESC;
    u32 ls = readl(desc(g, RDMA_OFF, i) + DESC_LEN_STATUS);
    u32 len = (ls >> DMA_BUFLENGTH_SHIFT) & 0xfff;
    u32 flags = ls & 0xffff;
    if ((flags & (DMA_SOP | DMA_EOP)) != (DMA_SOP | DMA_EOP) || (flags & DMA_RX_ERRORS) || len < 2 + 14 ||
        len > BUF_LEN) {
      nd->rx_errors++;
    } else {
      len -= 2; /* RBUF_ALIGN_2B */
      if (g->crc_fwd) len -= 4;
      netdev_rx(nd, g->rxbuf + (size_t)i * BUF_LEN + 2, len);
    }
    g->rx_c_index++;
    wr(g, RING_REGS(RDMA_OFF) + R_CONS_INDEX, g->rx_c_index);
    n++;
  }
  /* re-enable the receive interrupt once the ring is drained */
  if (n < budget) wr(g, INTRL2_0_OFF + INTRL2_MASK_CLEAR, IRQ_RXDMA_DONE);
  return n;
}

static int genet_xmit(struct net_device *nd, const void *frame, size_t len) {
  struct genet *g = nd->priv;
  if (!g->link) return -ENETDOWN;
  if (len > MAX_FRAME - 4) return -EMSGSIZE;
  g->tx_cons = rd(g, RING_REGS(TDMA_OFF) + T_CONS_INDEX) & 0xffff;
  if ((u16)(g->tx_prod - g->tx_cons) >= TOTAL_DESC - 1) return -EBUSY;
  int i = g->tx_prod % TOTAL_DESC;
  u8 *b = g->txbuf + (size_t)i * BUF_LEN;
  memcpy(b, frame, len);
  if (len < 60) { /* the MAC does not pad short frames */
    memset(b + len, 0, 60 - len);
    len = 60;
  }
  u32 ls = (u32)len << DMA_BUFLENGTH_SHIFT | QTAG_MASK << DMA_TX_QTAG_SHIFT | DMA_SOP | DMA_EOP | DMA_TX_APPEND_CRC;
  __asm__ volatile("dsb sy" ::: "memory");
  writel(ls, desc(g, TDMA_OFF, i) + DESC_LEN_STATUS);
  g->tx_prod++;
  wr(g, RING_REGS(TDMA_OFF) + T_PROD_INDEX, g->tx_prod);
  return 0;
}

static void genet_irq(int irq, void *arg) {
  struct genet *g = arg;
  u32 st = rd(g, INTRL2_0_OFF + INTRL2_STAT) & ~rd(g, INTRL2_0_OFF + INTRL2_MASK_STATUS);
  wr(g, INTRL2_0_OFF + INTRL2_CLEAR, st);
  if (st & IRQ_RXDMA_DONE) {
    wr(g, INTRL2_0_OFF + INTRL2_MASK_SET, IRQ_RXDMA_DONE); /* until netd drains the ring */
    netdev_schedule(&g->nd);
  }
}

static void link_tick(struct ktimer *t) {
  struct genet *g = t->arg;
  g->check_link = true;
  netdev_schedule(&g->nd);
  ktimer_start(&g->link_timer, ktime_ns() + NSEC_PER_SEC);
}

static void set_mdf(struct genet *g, int idx, const u8 *mac) {
  wr(g, UMAC_OFF + UMAC_MDF_ADDR + idx * 8, (u32)mac[0] << 8 | mac[1]);
  wr(g, UMAC_OFF + UMAC_MDF_ADDR + idx * 8 + 4, (u32)mac[2] << 24 | (u32)mac[3] << 16 | (u32)mac[4] << 8 | mac[5]);
}

/* Receive filter: broadcast, our address, and the multicast groups IPv4
 * and IPv6 need (all-hosts, all-nodes, our solicited-node group). */
static void program_filter(struct genet *g) {
  const u8 *m = g->nd.mac;
  const u8 list[][6] = {
      {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, {m[0], m[1], m[2], m[3], m[4], m[5]}, {0x01, 0x00, 0x5e, 0x00, 0x00, 0x01},
      {0x33, 0x33, 0x00, 0x00, 0x00, 0x01}, {0x33, 0x33, 0xff, m[3], m[4], m[5]},
  };
  u32 ctrl = 0;
  for (unsigned i = 0; i < ARRAY_SIZE(list); i++) {
    set_mdf(g, (int)i, list[i]);
    ctrl |= 1u << (MAX_MDF - 1 - i);
  }
  wr(g, UMAC_OFF + UMAC_MDF_CTRL, ctrl);
  wr(g, UMAC_OFF + UMAC_CMD, rd(g, UMAC_OFF + UMAC_CMD) & ~CMD_PROMISC);
}

static const struct net_device_ops genet_ops = {.xmit = genet_xmit, .poll = genet_poll};

static int get_mac(struct genet *g) {
  int len;
  const u8 *m = fdt_getprop(g->node, "local-mac-address", &len);
  if (!m || len != 6 || !(m[0] | m[1] | m[2] | m[3] | m[4] | m[5])) m = fdt_getprop(g->node, "mac-address", &len);
  if (m && len == 6 && (m[0] | m[1] | m[2] | m[3] | m[4] | m[5])) {
    memcpy(g->nd.mac, m, 6);
    return 0;
  }
  u8 v[8] = {0};
  if (rpi_fw_property(RPI_FW_GET_BOARD_MAC_ADDRESS, v, 0, 8) >= 6 && (v[0] | v[1] | v[2] | v[3] | v[4] | v[5])) {
    memcpy(g->nd.mac, v, 6);
    return 0;
  }
  return -ENODEV;
}

static int genet_probe(int node) {
  struct genet *g = kzalloc(sizeof(*g), 0);
  if (!g) return -ENOMEM;
  g->node = node;
  g->base = dt_ioremap(node, 0, NULL);
  if (!g->base) return -ENOMEM;
  u32 rev = rd(g, SYS_OFF);
  if (((rev >> 24) & 0xf) != 6 && ((rev >> 24) & 0xf) != 5) { /* GENET v5 reports major 6 */
    pr_warn("genet: unexpected revision %#x\n", rev);
    return -ENODEV;
  }
  if (get_mac(g)) {
    pr_warn("genet: no MAC address\n");
    return -ENODEV;
  }
  /* PHY address from the device tree (phy-handle -> reg), default 1 */
  g->phy = 1;
  u32 ph, reg;
  if (fdt_getprop_u32(node, "phy-handle", &ph)) {
    int pn = fdt_node_by_phandle(ph);
    if (pn >= 0 && fdt_getprop_u32(pn, "reg", &reg)) g->phy = (int)reg;
  }

  /* reset the UniMAC and the receive buffer */
  wr(g, SYS_OFF + SYS_RBUF_FLUSH_CTRL, 0);
  udelay(10);
  wr(g, UMAC_OFF + UMAC_CMD, CMD_SW_RESET);
  udelay(2);
  u32 f = rd(g, SYS_OFF + SYS_RBUF_FLUSH_CTRL);
  wr(g, SYS_OFF + SYS_RBUF_FLUSH_CTRL, f | 2);
  udelay(10);
  wr(g, SYS_OFF + SYS_RBUF_FLUSH_CTRL, f & ~2u);
  udelay(10);
  wr(g, UMAC_OFF + UMAC_MIB_CTRL, 7); /* reset counters */
  wr(g, UMAC_OFF + UMAC_MIB_CTRL, 0);
  wr(g, UMAC_OFF + UMAC_MAX_FRAME_LEN, MAX_FRAME);
  wr(g, TBUF_OFF + TBUF_CTRL, rd(g, TBUF_OFF + TBUF_CTRL) & ~RBUF_64B_EN); /* no transmit status block */
  wr(g, RBUF_OFF + RBUF_CTRL, (rd(g, RBUF_OFF + RBUF_CTRL) & ~RBUF_64B_EN) | RBUF_ALIGN_2B); /* 2-byte IP alignment */
  wr(g, RBUF_OFF + RBUF_TBUF_SIZE_CTRL, 1);
  wr(g, INTRL2_0_OFF + INTRL2_MASK_SET, ~0u);
  wr(g, INTRL2_0_OFF + INTRL2_CLEAR, ~0u);
  wr(g, HFB_REG_OFF, 0); /* no hardware filter block rules */
  g->crc_fwd = rd(g, UMAC_OFF + UMAC_CMD) & CMD_CRC_FWD;
  const u8 *m = g->nd.mac;
  wr(g, UMAC_OFF + UMAC_MAC0, (u32)m[0] << 24 | (u32)m[1] << 16 | (u32)m[2] << 8 | m[3]);
  wr(g, UMAC_OFF + UMAC_MAC1, (u32)m[4] << 8 | m[5]);

  /* RGMII to an external gigabit PHY; the MAC adds the TX delay unless the
   * PHY is told to (rgmii-txid / rgmii-id) */
  const char *mode = fdt_getprop_str(node, "phy-mode");
  wr(g, SYS_OFF + SYS_PORT_CTRL, PORT_MODE_EXT_GPHY);
  u32 oob = rd(g, EXT_OFF + EXT_RGMII_OOB_CTRL) & ~ID_MODE_DIS;
  if (mode && !strcmp(mode, "rgmii")) oob |= ID_MODE_DIS;
  wr(g, EXT_OFF + EXT_RGMII_OOB_CTRL, oob | RGMII_MODE_EN);

  /* DMA: stop, flush, then program ring 16 */
  wr(g, DMA_REGS(TDMA_OFF) + DMA_CTRL, rd(g, DMA_REGS(TDMA_OFF) + DMA_CTRL) & ~(DMA_EN | DMA_RING16_EN));
  wr(g, DMA_REGS(RDMA_OFF) + DMA_CTRL, rd(g, DMA_REGS(RDMA_OFF) + DMA_CTRL) & ~(DMA_EN | DMA_RING16_EN));
  wr(g, UMAC_OFF + UMAC_TX_FLUSH, 1);
  udelay(10);
  wr(g, UMAC_OFF + UMAC_TX_FLUSH, 0);
  f = rd(g, SYS_OFF + SYS_RBUF_FLUSH_CTRL);
  wr(g, SYS_OFF + SYS_RBUF_FLUSH_CTRL, f | 1);
  udelay(10);
  wr(g, SYS_OFF + SYS_RBUF_FLUSH_CTRL, f);
  udelay(10);

  g->rxbuf = dma_alloc_coherent((size_t)TOTAL_DESC * BUF_LEN, &g->rxpa, GFP_DMA32);
  g->txbuf = dma_alloc_coherent((size_t)TOTAL_DESC * BUF_LEN, &g->txpa, GFP_DMA32);
  if (!g->rxbuf || !g->txbuf) return -ENOMEM;
  for (int i = 0; i < TOTAL_DESC; i++) {
    set_desc_addr(g, RDMA_OFF, i, dt_dma_addr(node, g->rxpa + (phys_addr_t)i * BUF_LEN));
    set_desc_addr(g, TDMA_OFF, i, dt_dma_addr(node, g->txpa + (phys_addr_t)i * BUF_LEN));
  }
  u32 rr = RING_REGS(RDMA_OFF), tr = RING_REGS(TDMA_OFF), words = DESC_SIZE / 4;
  wr(g, DMA_REGS(RDMA_OFF) + DMA_SCB_BURST_SIZE, 8);
  wr(g, rr + R_PROD_INDEX, 0);
  wr(g, rr + R_CONS_INDEX, 0);
  wr(g, rr + R_BUF_SIZE, (u32)TOTAL_DESC << 16 | BUF_LEN);
  wr(g, rr + R_XON_XOFF, 5u << 16 | (TOTAL_DESC >> 4));
  wr(g, rr + R_START_ADDR, 0);
  wr(g, rr + R_READ_PTR, 0);
  wr(g, rr + R_WRITE_PTR, 0);
  wr(g, rr + R_END_ADDR, TOTAL_DESC * words - 1);
  wr(g, rr + R_MBUF_DONE_THRESH, 1);
  wr(g, DMA_REGS(TDMA_OFF) + DMA_SCB_BURST_SIZE, 8);
  wr(g, tr + T_PROD_INDEX, 0);
  wr(g, tr + T_CONS_INDEX, 0);
  wr(g, tr + R_MBUF_DONE_THRESH, 10);
  wr(g, tr + T_FLOW_PERIOD, 0);
  wr(g, tr + R_BUF_SIZE, (u32)TOTAL_DESC << 16 | BUF_LEN);
  wr(g, tr + R_START_ADDR, 0);
  wr(g, tr + T_READ_PTR, 0);
  wr(g, tr + T_WRITE_PTR, 0);
  wr(g, tr + R_END_ADDR, TOTAL_DESC * words - 1);
  wr(g, DMA_REGS(RDMA_OFF) + DMA_RING_CFG, 1u << DESC_INDEX);
  wr(g, DMA_REGS(TDMA_OFF) + DMA_RING_CFG, 1u << DESC_INDEX);
  wr(g, DMA_REGS(RDMA_OFF) + DMA_CTRL, DMA_EN | DMA_RING16_EN);
  wr(g, DMA_REGS(TDMA_OFF) + DMA_CTRL, DMA_EN | DMA_RING16_EN);

  strlcpy(g->nd.name, "genet", sizeof(g->nd.name));
  if (phy_init(g)) {
    pr_warn("genet: no PHY on the MDIO bus\n");
    return -ENODEV;
  }
  program_filter(g);

  u32 irq, fl;
  if (!fdt_get_irq(node, 0, &irq, &fl)) request_irq(irq, genet_irq, g, "genet");
  wr(g, INTRL2_0_OFF + INTRL2_MASK_CLEAR, IRQ_RXDMA_DONE);

  g->nd.mtu = 1500;
  g->nd.ops = &genet_ops;
  g->nd.priv = g;
  g->nd.link_up = false;
  int r = netdev_register(&g->nd);
  if (r) return r;
  pr_info("%s: GENET v%u.%u, RGMII\n", g->nd.name, (rev >> 24) & 0xf, (rev >> 16) & 0xf);
  ktimer_init(&g->link_timer, link_tick, g);
  ktimer_start(&g->link_timer, ktime_ns() + NSEC_PER_SEC);
  return 0;
}

DT_DRIVER(bcmgenet, DRV_DEVICE, genet_probe, "brcm,bcm2711-genet-v5", "brcm,genet-v5");
