/* ARM PL031 real-time clock (QEMU virt): a free-running seconds counter. */
#include <olux/device.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/rtc.h>

#define RTC_DR 0x00 /* data (current seconds) */
#define RTC_LR 0x08 /* load */
#define RTC_CR 0x0c /* control: bit 0 starts the counter */

static int pl031_read(void *priv, u64 *secs) {
  *secs = readl((u8 *)priv + RTC_DR);
  return 0;
}

static int pl031_set(void *priv, u64 secs) {
  if (secs > 0xffffffffULL) return -ERANGE; /* 32-bit counter: until 2106 */
  writel((u32)secs, (u8 *)priv + RTC_LR);
  return 0;
}

static const struct rtc_ops pl031_ops = {.name = "pl031", .read = pl031_read, .set = pl031_set};

static int pl031_probe(int node) {
  u8 *base = dt_ioremap(node, 0, NULL);
  if (!base) return -ENOMEM;
  if (!(readl(base + RTC_CR) & 1)) writel(1, base + RTC_CR);
  return rtc_register(&pl031_ops, base);
}

DT_DRIVER(pl031, DRV_DEVICE, pl031_probe, "arm,pl031");
