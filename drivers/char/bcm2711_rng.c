/*
 * BCM2711 RNG200 (and BCM2835 legacy RNG) hardware random number generator.
 * Seeds the kernel CSPRNG at boot and mixes in fresh output every minute.
 * QEMU's raspi4b model has the legacy register layout at the RNG200's
 * address, so an RNG200 that produces nothing falls back to that layout.
 */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/kernel.h>
#include <olux/random.h>
#include <olux/time.h>

/* RNG200 */
#define RNG_CTRL 0x00
#define RNG_SOFT_RESET 0x04
#define RBG_SOFT_RESET 0x08
#define RNG_TOTAL_BIT_COUNT_THRESHOLD 0x10
#define RNG_INT_STATUS 0x18
#define RNG_FIFO_DATA 0x20
#define RNG_FIFO_COUNT 0x24
/* BCM2835 */
#define RNG2835_CTRL 0x00
#define RNG2835_STATUS 0x04
#define RNG2835_DATA 0x08

#define RESEED_NS (60ULL * NSEC_PER_SEC)

static void *base;
static bool legacy;
static struct ktimer reseed_timer;

static unsigned avail_words(void) {
  return legacy ? readl(base + RNG2835_STATUS) >> 24 : readl(base + RNG_FIFO_COUNT) & 0xff;
}

static u32 read_word(void) { return readl(base + (legacy ? RNG2835_DATA : RNG_FIFO_DATA)); }

/* Collect up to `n` words without waiting. */
static unsigned harvest(u32 *out, unsigned n) {
  unsigned got = 0;
  while (got < n && avail_words()) out[got++] = read_word();
  return got;
}

static bool wait_data(u64 ns) {
  u64 end = ktime_ns() + ns;
  while (!avail_words())
    if (ktime_ns() > end) return false;
  return true;
}

static void init_rng200(void) {
  writel(readl(base + RNG_CTRL) & ~0x1fffu, base + RNG_CTRL); /* disable */
  writel(~0u, base + RNG_INT_STATUS);
  writel(1, base + RNG_SOFT_RESET);
  writel(1, base + RBG_SOFT_RESET);
  writel(0, base + RNG_SOFT_RESET);
  writel(0, base + RBG_SOFT_RESET);
  writel(0x40000, base + RNG_TOTAL_BIT_COUNT_THRESHOLD);
  writel((3u << 13) | 1, base + RNG_CTRL); /* sample-clock divider, enable */
}

static void init_legacy(void) {
  writel(0x40000, base + RNG2835_STATUS); /* discard the warm-up output */
  writel(1, base + RNG2835_CTRL);
}

static void reseed(struct ktimer *t) {
  u32 buf[8];
  unsigned n = harvest(buf, 8);
  if (n) add_entropy(buf, n * 4, n * 32);
  ktimer_start(&reseed_timer, ktime_ns() + RESEED_NS);
}

static int rng_probe(int node) {
  base = dt_ioremap(node, 0, NULL);
  if (!base) return -ENOMEM;
  legacy = fdt_is_compatible(node, "brcm,bcm2835-rng");
  if (legacy)
    init_legacy();
  else
    init_rng200();
  if (!wait_data(200 * NSEC_PER_MSEC) && !legacy) {
    legacy = true; /* e.g. QEMU: legacy block at the RNG200 address */
    init_legacy();
    if (!wait_data(200 * NSEC_PER_MSEC)) {
      pr_warn("hwrng: no output from the RNG; not used\n");
      return -EIO;
    }
  }
  u32 buf[16];
  unsigned n = 0;
  for (int tries = 0; n < 16 && tries < 1000; tries++) {
    n += harvest(buf + n, 16 - n);
    if (n < 16) udelay(10);
  }
  add_entropy(buf, n * 4, n * 32);
  pr_info("hwrng: %s RNG, seeded CSPRNG with %u bytes\n", legacy ? "BCM2835" : "RNG200", n * 4);
  ktimer_init(&reseed_timer, reseed, NULL);
  ktimer_start(&reseed_timer, ktime_ns() + RESEED_NS);
  return 0;
}

DT_DRIVER(bcm_rng, DRV_DEVICE, rng_probe, "brcm,bcm2711-rng200", "brcm,bcm2835-rng", "brcm,bcm2838-rng200");
