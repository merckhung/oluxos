/*
 * SD Host Controller (SDHCI v2/v3) driver with the SD memory-card protocol:
 * BCM2711 EMMC2 (the Pi 4 SD slot) and the BCM2835 Arasan controller.
 *
 * Data moves by PIO through the buffer data port. Registers are accessed
 * 32 bits at a time only (the Arasan block ignores narrower writes), and
 * waits poll the status register, sleeping between polls, so the driver
 * does not depend on the (shared) interrupt line. Failed transfers reset
 * the command/data lines, stop the card and are retried.
 */
#include <olux/blkdev.h>
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/rpi_firmware.h>
#include <olux/sched.h>
#include <olux/time.h>

#define SDHCI_BLOCK 0x04 /* size (bits 11:0) | boundary | count << 16 */
#define SDHCI_ARG 0x08
#define SDHCI_CMD 0x0c /* transfer mode | command << 16 */
#define SDHCI_RESP0 0x10
#define SDHCI_DATA 0x20
#define SDHCI_PRESENT 0x24
#define SDHCI_HOST 0x28  /* host ctrl | power << 8 | gap << 16 | wakeup << 24 */
#define SDHCI_CLOCK 0x2c /* clock | timeout << 16 | reset << 24 */
#define SDHCI_INT 0x30   /* normal | error << 16 (write 1 to clear) */
#define SDHCI_INT_EN 0x34
#define SDHCI_SIG_EN 0x38
#define SDHCI_CAPS 0x40
#define SDHCI_VERSION 0xfc /* host version in bits 23:16 */

#define PRESENT_CMD_INHIBIT (1u << 0)
#define PRESENT_DAT_INHIBIT (1u << 1)
#define PRESENT_CARD (1u << 16)

#define INT_CMD_DONE (1u << 0)
#define INT_XFER_DONE (1u << 1)
#define INT_WRITE_READY (1u << 4)
#define INT_READ_READY (1u << 5)
#define INT_ERROR (1u << 15)
#define INT_ERR_MASK 0xffff0000u

#define HOST_4BIT (1u << 1)
#define HOST_HIGH_SPEED (1u << 2)
#define POWER_ON_33V ((0x7u << 1 | 1u) << 8)

#define CLOCK_INT_EN (1u << 0)
#define CLOCK_INT_STABLE (1u << 1)
#define CLOCK_CARD_EN (1u << 2)
#define RESET_ALL (1u << 24)
#define RESET_CMD (1u << 25)
#define RESET_DAT (1u << 26)

/* command register flags */
#define RSP_NONE 0x00
#define RSP_136 0x09 /* R2: long, CRC */
#define RSP_48 0x1a  /* R1/R6/R7: CRC + index */
#define RSP_48B 0x1b /* R1b: busy */
#define RSP_R3 0x02  /* OCR: no CRC/index */
#define CMD_DATA 0x20
/* transfer mode */
#define TM_BLKCNT (1u << 1)
#define TM_AUTO12 (1u << 2)
#define TM_READ (1u << 4)
#define TM_MULTI (1u << 5)

#define CMD_TIMEOUT_NS (200 * NSEC_PER_MSEC)
#define DATA_TIMEOUT_NS (2000 * NSEC_PER_MSEC)

struct sdhci {
  void *base;
  int node;
  bool arasan; /* BCM2835 Arasan: slow register writes at low clock */
  u32 base_clock;
  u32 clock;
  u32 rca;
  bool sdhc; /* block addressing */
  u32 resp[4];
  struct blkdev bd;
};

static u32 rd(struct sdhci *h, u32 reg) { return readl(h->base + reg); }

static void wr(struct sdhci *h, u32 reg, u32 v) {
  writel(v, h->base + reg);
  if (h->arasan && h->clock <= 400000) udelay(10); /* 2 SD clocks between writes */
}

/* Wait for a status condition; sleeps between polls. */
static bool poll(struct sdhci *h, u32 reg, u32 mask, u32 want, u64 timeout_ns) {
  u64 end = ktime_ns() + timeout_ns;
  for (int i = 0;; i++) {
    if ((rd(h, reg) & mask) == want) return true;
    if (ktime_ns() > end) return (rd(h, reg) & mask) == want;
    if (i < 50)
      udelay(2);
    else
      sleep_ns(50 * 1000);
  }
}

static void reset_lines(struct sdhci *h, u32 which) {
  wr(h, SDHCI_CLOCK, rd(h, SDHCI_CLOCK) | which);
  poll(h, SDHCI_CLOCK, which, 0, 100 * NSEC_PER_MSEC);
}

static int set_clock(struct sdhci *h, u32 hz) {
  u32 c = rd(h, SDHCI_CLOCK) & ~0xffffu;
  wr(h, SDHCI_CLOCK, c); /* stop the clock */
  u32 div = 0;           /* 10-bit divided clock: base / (2 * div) */
  if (hz < h->base_clock) {
    div = (h->base_clock + 2 * hz - 1) / (2 * hz);
    if (div > 0x3ff) div = 0x3ff;
  }
  c |= (div & 0xff) << 8 | ((div >> 8) & 3) << 6 | CLOCK_INT_EN;
  c = (c & ~(0xfu << 16)) | 0xeu << 16; /* longest data timeout */
  wr(h, SDHCI_CLOCK, c);
  if (!poll(h, SDHCI_CLOCK, CLOCK_INT_STABLE, CLOCK_INT_STABLE, 20 * NSEC_PER_MSEC)) return -ETIMEDOUT;
  wr(h, SDHCI_CLOCK, rd(h, SDHCI_CLOCK) | CLOCK_CARD_EN);
  h->clock = div ? h->base_clock / (2 * div) : h->base_clock;
  udelay(100);
  return 0;
}

/* Issue a command without data. Returns 0 or -errno; response in h->resp. */
static int send_cmd(struct sdhci *h, u32 idx, u32 arg, u32 flags, u32 mode) {
  u32 inhibit = PRESENT_CMD_INHIBIT | ((flags & CMD_DATA) || (flags & 3) == 3 ? PRESENT_DAT_INHIBIT : 0);
  if (!poll(h, SDHCI_PRESENT, inhibit, 0, CMD_TIMEOUT_NS)) {
    reset_lines(h, RESET_CMD | RESET_DAT);
    return -EBUSY;
  }
  wr(h, SDHCI_INT, ~0u); /* clear stale status */
  wr(h, SDHCI_ARG, arg);
  wr(h, SDHCI_CMD, (idx << 24) | (flags << 16) | mode);
  if (!poll(h, SDHCI_INT, INT_CMD_DONE | INT_ERROR, INT_CMD_DONE, CMD_TIMEOUT_NS) &&
      !(rd(h, SDHCI_INT) & (INT_CMD_DONE | INT_ERROR))) {
    reset_lines(h, RESET_CMD);
    return -ETIMEDOUT;
  }
  u32 st = rd(h, SDHCI_INT);
  if (st & INT_ERROR) {
    wr(h, SDHCI_INT, st);
    reset_lines(h, RESET_CMD);
    return (st >> 16) & 1 ? -ETIMEDOUT : -EIO; /* bit 16: command timeout */
  }
  wr(h, SDHCI_INT, INT_CMD_DONE);
  for (int i = 0; i < 4; i++) h->resp[i] = rd(h, SDHCI_RESP0 + 4 * i);
  if ((flags & 3) == 3 && !(flags & CMD_DATA)) { /* R1b: wait for busy to end */
    if (!poll(h, SDHCI_INT, INT_XFER_DONE | INT_ERROR, INT_XFER_DONE, DATA_TIMEOUT_NS)) {
      reset_lines(h, RESET_DAT);
      return -ETIMEDOUT;
    }
    wr(h, SDHCI_INT, INT_XFER_DONE);
  }
  return 0;
}

static int app_cmd(struct sdhci *h, u32 idx, u32 arg, u32 flags) {
  int r = send_cmd(h, 55, h->rca << 16, RSP_48, 0);
  return r ? r : send_cmd(h, idx, arg, flags, 0);
}

/* Bits [hi:lo] of a 128-bit R2 response (CRC byte stripped by the host). */
static u32 r2_bits(const u32 *resp, int hi, int lo) {
  u32 v = 0;
  for (int b = hi; b >= lo; b--) {
    int bit = b - 8;
    v = v << 1 | ((resp[bit / 32] >> (bit % 32)) & 1);
  }
  return v;
}

/* One data transfer; `count` blocks of 512 bytes. */
static int xfer(struct sdhci *h, u64 sector, u8 *buf, u32 count, bool write) {
  u32 arg = h->sdhc ? (u32)sector : (u32)(sector * 512);
  bool multi = count > 1;
  u32 idx = write ? (multi ? 25 : 24) : (multi ? 18 : 17);
  u32 mode = (write ? 0 : TM_READ) | (multi ? TM_MULTI | TM_BLKCNT | TM_AUTO12 : 0);
  wr(h, SDHCI_BLOCK, count << 16 | 7u << 12 | 512);
  int r = send_cmd(h, idx, arg, RSP_48 | CMD_DATA, mode);
  if (r) return r;
  u32 ready = write ? INT_WRITE_READY : INT_READ_READY;
  for (u32 b = 0; b < count; b++) {
    if (!poll(h, SDHCI_INT, ready | INT_ERROR, ready, DATA_TIMEOUT_NS)) return -EIO;
    wr(h, SDHCI_INT, ready);
    u8 *p = buf + (size_t)b * 512;
    for (int w = 0; w < 128; w++) {
      u32 v;
      if (write) {
        memcpy(&v, p + w * 4, 4);
        writel(v, h->base + SDHCI_DATA);
      } else {
        v = readl(h->base + SDHCI_DATA);
        memcpy(p + w * 4, &v, 4);
      }
    }
  }
  if (!poll(h, SDHCI_INT, INT_XFER_DONE | INT_ERROR, INT_XFER_DONE, DATA_TIMEOUT_NS)) return -EIO;
  wr(h, SDHCI_INT, INT_XFER_DONE);
  return 0;
}

static int sd_rw(struct blkdev *bd, u64 sector, void *buf, u32 count, bool write) {
  struct sdhci *h = bd->priv;
  int r = 0;
  while (count) {
    u32 n = MIN(count, 1024u);
    for (int attempt = 0; attempt < 3; attempt++) {
      r = xfer(h, sector, buf, n, write);
      if (!r) break;
      /* recover: clear errors, reset the lines, stop the card, wait for it */
      wr(h, SDHCI_INT, ~0u);
      reset_lines(h, RESET_CMD | RESET_DAT);
      send_cmd(h, 12, 0, RSP_48B, 0);
      for (int i = 0; i < 100; i++) {
        if (!send_cmd(h, 13, h->rca << 16, RSP_48, 0) && ((h->resp[0] >> 9) & 0xf) == 4) break; /* tran */
        sleep_ns(NSEC_PER_MSEC);
      }
    }
    if (r) {
      pr_err("%s: %s of %u sectors at %llu failed: %d\n", bd->name, write ? "write" : "read", n,
             (unsigned long long)sector, r);
      return -EIO;
    }
    sector += n;
    buf = (u8 *)buf + (size_t)n * 512;
    count -= n;
  }
  return 0;
}

static const struct blkdev_ops sd_ops = {.rw = sd_rw};

/* Try to switch the card to high-speed (50 MHz) mode with CMD6. */
static void try_high_speed(struct sdhci *h) {
  u8 status[64];
  wr(h, SDHCI_BLOCK, 1u << 16 | 64);
  if (send_cmd(h, 6, 0x80fffff1, RSP_48 | CMD_DATA, TM_READ)) return;
  if (!poll(h, SDHCI_INT, INT_READ_READY | INT_ERROR, INT_READ_READY, DATA_TIMEOUT_NS)) {
    reset_lines(h, RESET_CMD | RESET_DAT);
    return;
  }
  wr(h, SDHCI_INT, INT_READ_READY);
  for (int w = 0; w < 16; w++) {
    u32 v = readl(h->base + SDHCI_DATA);
    memcpy(status + w * 4, &v, 4);
  }
  poll(h, SDHCI_INT, INT_XFER_DONE, INT_XFER_DONE, DATA_TIMEOUT_NS);
  wr(h, SDHCI_INT, INT_XFER_DONE);
  if ((status[16] & 0xf) != 1) return; /* function group 1 not switched to high speed */
  wr(h, SDHCI_HOST, rd(h, SDHCI_HOST) | HOST_HIGH_SPEED);
  set_clock(h, 50000000);
}

static int card_init(struct sdhci *h) {
  send_cmd(h, 0, 0, RSP_NONE, 0);
  bool v2 = !send_cmd(h, 8, 0x1aa, RSP_48, 0) && (h->resp[0] & 0xfff) == 0x1aa;
  u64 end = ktime_ns() + 1000 * NSEC_PER_MSEC;
  for (;;) {
    int r = app_cmd(h, 41, (v2 ? 0x40000000u : 0) | 0x00ff8000u, RSP_R3);
    if (r) return r == -ETIMEDOUT ? -ENODEV : r; /* no SD memory card (e.g. SDIO WiFi) */
    if (h->resp[0] & 0x80000000u) break;
    if (ktime_ns() > end) return -ETIMEDOUT;
    sleep_ns(10 * NSEC_PER_MSEC);
  }
  h->sdhc = h->resp[0] & 0x40000000u;
  if (send_cmd(h, 2, 0, RSP_136, 0)) return -EIO;
  u32 cid[4];
  memcpy(cid, h->resp, sizeof(cid));
  if (send_cmd(h, 3, 0, RSP_48, 0)) return -EIO;
  h->rca = h->resp[0] >> 16;
  if (send_cmd(h, 9, h->rca << 16, RSP_136, 0)) return -EIO;
  u64 sectors;
  if (r2_bits(h->resp, 127, 126) == 1) { /* CSD 2.0 */
    sectors = ((u64)r2_bits(h->resp, 69, 48) + 1) * 1024;
  } else {
    u32 c_size = r2_bits(h->resp, 73, 62), mult = r2_bits(h->resp, 49, 47), bl = r2_bits(h->resp, 83, 80);
    sectors = ((u64)(c_size + 1) << (mult + 2)) << bl >> 9;
  }
  if (send_cmd(h, 7, h->rca << 16, RSP_48B, 0)) return -EIO;
  if (!app_cmd(h, 6, 2, RSP_48)) wr(h, SDHCI_HOST, rd(h, SDHCI_HOST) | HOST_4BIT);
  if (!h->sdhc) send_cmd(h, 16, 512, RSP_48, 0);
  set_clock(h, 25000000);
  try_high_speed(h);
  char name[6];
  for (int i = 0; i < 5; i++) name[i] = (char)r2_bits(cid, 103 - 8 * i, 96 - 8 * i);
  name[5] = 0;
  h->bd.nr_sectors = sectors;
  pr_info("mmc: %s card '%s', %llu MiB, %s, %u kHz\n", h->sdhc ? "SDHC/SDXC" : "SDSC", name,
          (unsigned long long)(sectors >> 11), (rd(h, SDHCI_HOST) & HOST_4BIT) ? "4-bit" : "1-bit", h->clock / 1000);
  return 0;
}

static int sdhci_probe(int node) {
  static int ncards;
  struct sdhci *h = kzalloc(sizeof(*h), 0);
  if (!h) return -ENOMEM;
  h->base = dt_ioremap(node, 0, NULL);
  if (!h->base) return -ENOMEM;
  h->node = node;
  h->arasan = fdt_is_compatible(node, "brcm,bcm2835-sdhci");
  wr(h, SDHCI_CLOCK, RESET_ALL);
  if (!poll(h, SDHCI_CLOCK, RESET_ALL, 0, 100 * NSEC_PER_MSEC)) return -EIO;
  u32 caps = rd(h, SDHCI_CAPS);
  h->base_clock = ((caps >> 8) & 0xff) * 1000000;
  u32 fw_hz = 0;
  if (rpi_fw_available() && !rpi_fw_clock_rate(h->arasan ? RPI_CLK_EMMC : RPI_CLK_EMMC2, &fw_hz) && fw_hz)
    h->base_clock = fw_hz;
  u32 dt_hz;
  if (!h->base_clock && fdt_getprop_u32(node, "clock-frequency", &dt_hz)) h->base_clock = dt_hz;
  if (!h->base_clock) h->base_clock = 100000000;
  wr(h, SDHCI_HOST, POWER_ON_33V);
  wr(h, SDHCI_INT_EN, ~0u); /* status bits latch; signalling stays off */
  wr(h, SDHCI_SIG_EN, 0);
  if (set_clock(h, 400000)) return -EIO;
  sleep_ns(2 * NSEC_PER_MSEC); /* 74 clocks of power-up */
  int r = card_init(h);
  if (r) {
    kfree(h);
    return r == -ENODEV ? -ENODEV : r;
  }
  snprintf(h->bd.name, sizeof(h->bd.name), "mmcblk%d", ncards++);
  h->bd.ops = &sd_ops;
  h->bd.priv = h;
  return blkdev_register(&h->bd, 179, "p") ? -EIO : 0;
}

DT_DRIVER(sdhci, DRV_DEVICE, sdhci_probe, "brcm,bcm2711-emmc2", "brcm,bcm2835-sdhci", "brcm,bcm2835-mmc");
