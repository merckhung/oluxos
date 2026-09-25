/*
 * BCM2835/BCM2711 SPI master (SPI0 family), exported as /dev/spidevB.C with
 * the Linux spidev ABI: SPI_IOC_MESSAGE full-duplex transfers, mode, bits
 * per word and speed ioctls, and half-duplex read()/write(). Polled PIO.
 */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/fs.h>
#include <olux/gpio.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/rpi_firmware.h>
#include <olux/sched.h>
#include <olux/time.h>
#include <olux/uaccess.h>
#include <olux/wait.h>

#define SPI_CS 0x00
#define SPI_FIFO 0x04
#define SPI_CLK 0x08
#define SPI_DLEN 0x0c

#define CS_CPHA (1u << 2)
#define CS_CPOL (1u << 3)
#define CS_CLEAR (3u << 4)
#define CS_CSPOL (1u << 6)
#define CS_TA (1u << 7)
#define CS_DONE (1u << 16)
#define CS_RXD (1u << 17)
#define CS_TXD (1u << 18)

#define SPI_CPHA 0x01
#define SPI_CPOL 0x02
#define SPI_CS_HIGH 0x04
#define SPI_LSB_FIRST 0x08
#define SPI_NO_CS 0x40

#define SPI_IOC_RD_MODE 0x80016b01u
#define SPI_IOC_WR_MODE 0x40016b01u
#define SPI_IOC_RD_LSB_FIRST 0x80016b02u
#define SPI_IOC_WR_LSB_FIRST 0x40016b02u
#define SPI_IOC_RD_BITS_PER_WORD 0x80016b03u
#define SPI_IOC_WR_BITS_PER_WORD 0x40016b03u
#define SPI_IOC_RD_MAX_SPEED_HZ 0x80046b04u
#define SPI_IOC_WR_MAX_SPEED_HZ 0x40046b04u
#define SPI_IOC_RD_MODE32 0x80046b05u
#define SPI_IOC_WR_MODE32 0x40046b05u
#define SPI_IOC_MESSAGE_BASE 0x40006b00u /* | (n * 32) << 16 */

#define MAX_XFER 65536
#define MAX_MSGS 64

struct spi_ioc_transfer {
  u64 tx_buf, rx_buf;
  u32 len, speed_hz;
  u16 delay_usecs;
  u8 bits_per_word, cs_change, tx_nbits, rx_nbits, word_delay_usecs, pad;
};

struct spi_bus {
  void *base;
  struct mutex lock;
  u32 core_hz;
  int id;
};

struct spidev {
  struct spi_bus *bus;
  unsigned cs;
  u32 mode, speed;
  u8 bits;
};

static void set_speed(struct spi_bus *b, u32 hz) {
  u32 div = hz ? (b->core_hz + hz - 1) / hz : 0;
  div = ALIGN_UP(div, 2);
  if (div < 2) div = 2;
  if (div > 65534) div = 0; /* 0 = divide by 65536 */
  writel(div, b->base + SPI_CLK);
}

/* One full-duplex transfer with CS asserted; tx or rx may be NULL. */
static int spi_xfer(struct spidev *d, const u8 *tx, u8 *rx, u32 len, u32 speed) {
  struct spi_bus *b = d->bus;
  set_speed(b, speed ? speed : d->speed);
  u32 cs = CS_CLEAR | (d->mode & SPI_CPHA ? CS_CPHA : 0) | (d->mode & SPI_CPOL ? CS_CPOL : 0) |
           (d->mode & SPI_CS_HIGH ? CS_CSPOL : 0) | (d->mode & SPI_NO_CS ? 3 : d->cs);
  writel(cs, b->base + SPI_CS);
  writel(cs | CS_TA, b->base + SPI_CS);
  u32 sent = 0, got = 0;
  u64 deadline = ktime_ns() + NSEC_PER_SEC + (u64)len * 8 * NSEC_PER_SEC / (speed ? speed : d->speed);
  for (int spins = 0; got < len; spins++) {
    u32 s = readl(b->base + SPI_CS);
    /* keep at most 64 bytes in flight so the RX FIFO cannot overflow */
    while (sent < len && sent - got < 64 && (s & CS_TXD)) {
      writel(tx ? tx[sent] : 0, b->base + SPI_FIFO);
      sent++;
      s = readl(b->base + SPI_CS);
    }
    while (got < sent && (s & CS_RXD)) {
      u8 v = (u8)readl(b->base + SPI_FIFO);
      if (rx) rx[got] = v;
      got++;
      s = readl(b->base + SPI_CS);
    }
    if (ktime_ns() > deadline) {
      writel(CS_CLEAR, b->base + SPI_CS);
      return -ETIMEDOUT;
    }
    if (spins > 1000) sleep_ns(10 * 1000);
  }
  while (!(readl(b->base + SPI_CS) & CS_DONE))
    if (ktime_ns() > deadline) break;
  writel(cs & ~CS_TA, b->base + SPI_CS);
  return 0;
}

static int spidev_open(struct inode *i, struct file *f) {
  struct spidev *d = kzalloc(sizeof(*d), 0);
  if (!d) return -ENOMEM;
  d->bus = f->priv;
  d->cs = MINOR(i->rdev) & 1;
  d->speed = 500000;
  d->bits = 8;
  f->priv = d;
  return 0;
}

static int spidev_release(struct inode *i, struct file *f) {
  kfree(f->priv);
  return 0;
}

static ssize_t spidev_rw(struct file *f, struct iobuf *b, bool rd) {
  struct spidev *d = f->priv;
  size_t len = MIN(b->len, (size_t)MAX_XFER);
  u8 *buf = kmalloc(len ? len : 1, 0);
  if (!buf) return -ENOMEM;
  int r = rd ? 0 : iob_read(b, 0, buf, len);
  if (!r) {
    mutex_lock(&d->bus->lock);
    r = spi_xfer(d, rd ? NULL : buf, rd ? buf : NULL, len, 0);
    mutex_unlock(&d->bus->lock);
  }
  if (!r && rd && iob_write(b, 0, buf, len)) r = -EFAULT;
  kfree(buf);
  return r ? r : (ssize_t)len;
}

static ssize_t spidev_read(struct file *f, struct iobuf *b, loff_t *pos) { return spidev_rw(f, b, true); }
static ssize_t spidev_write(struct file *f, struct iobuf *b, loff_t *pos) { return spidev_rw(f, b, false); }

static int do_message(struct spidev *d, u64 arg, unsigned n) {
  if (!n || n > MAX_MSGS) return -EINVAL;
  struct spi_ioc_transfer *t = kmalloc(n * sizeof(*t), 0);
  u8 *buf = kmalloc(MAX_XFER, 0);
  int r = t && buf ? 0 : -ENOMEM;
  if (!r && copy_from_user(t, arg, n * sizeof(*t))) r = -EFAULT;
  int total = 0;
  mutex_lock(&d->bus->lock);
  for (unsigned i = 0; i < n && !r; i++) {
    if (t[i].len > MAX_XFER || (t[i].bits_per_word && t[i].bits_per_word != 8)) {
      r = -EINVAL;
      break;
    }
    if (t[i].tx_buf && copy_from_user(buf, t[i].tx_buf, t[i].len))
      r = -EFAULT;
    else if (!t[i].tx_buf)
      memset(buf, 0, t[i].len);
    if (!r) r = spi_xfer(d, buf, buf, t[i].len, t[i].speed_hz);
    if (!r && t[i].rx_buf && copy_to_user(t[i].rx_buf, buf, t[i].len)) r = -EFAULT;
    if (t[i].delay_usecs) udelay(t[i].delay_usecs);
    total += t[i].len;
  }
  mutex_unlock(&d->bus->lock);
  kfree(t);
  kfree(buf);
  return r ? r : total;
}

static long spidev_ioctl(struct file *f, unsigned cmd, u64 arg) {
  struct spidev *d = f->priv;
  u8 v8;
  u32 v32;
  switch (cmd) {
    case SPI_IOC_RD_MODE:
      return put_user((u8)d->mode, (u8 *)arg);
    case SPI_IOC_RD_MODE32:
      return put_user(d->mode, (u32 *)arg);
    case SPI_IOC_WR_MODE:
      if (get_user(v8, (u8 *)arg)) return -EFAULT;
      v32 = v8;
      goto set_mode;
    case SPI_IOC_WR_MODE32:
      if (get_user(v32, (u32 *)arg)) return -EFAULT;
    set_mode:
      if (v32 & ~(u32)(SPI_CPHA | SPI_CPOL | SPI_CS_HIGH | SPI_NO_CS)) return -EINVAL;
      d->mode = v32;
      return 0;
    case SPI_IOC_RD_LSB_FIRST:
      return put_user((u8)0, (u8 *)arg);
    case SPI_IOC_WR_LSB_FIRST:
      if (get_user(v8, (u8 *)arg)) return -EFAULT;
      return v8 ? -EINVAL : 0;
    case SPI_IOC_RD_BITS_PER_WORD:
      return put_user(d->bits, (u8 *)arg);
    case SPI_IOC_WR_BITS_PER_WORD:
      if (get_user(v8, (u8 *)arg)) return -EFAULT;
      return v8 == 0 || v8 == 8 ? 0 : -EINVAL;
    case SPI_IOC_RD_MAX_SPEED_HZ:
      return put_user(d->speed, (u32 *)arg);
    case SPI_IOC_WR_MAX_SPEED_HZ:
      if (get_user(v32, (u32 *)arg)) return -EFAULT;
      if (!v32) return -EINVAL;
      d->speed = v32;
      return 0;
  }
  if ((cmd & 0xffff) == (SPI_IOC_MESSAGE_BASE & 0xffff) && (cmd >> 30) == 1) {
    u32 size = (cmd >> 16) & 0x3fff;
    if (size % sizeof(struct spi_ioc_transfer)) return -EINVAL;
    return do_message(d, arg, size / sizeof(struct spi_ioc_transfer));
  }
  return -ENOTTY;
}

static const struct file_operations spidev_fops = {
    .open = spidev_open, .release = spidev_release, .read = spidev_read, .write = spidev_write, .ioctl = spidev_ioctl};

static int spi_probe(int node) {
  static int next_id = 10;
  struct spi_bus *b = kzalloc(sizeof(*b), 0);
  if (!b) return -ENOMEM;
  b->base = dt_ioremap(node, 0, NULL);
  if (!b->base) return -ENOMEM;
  mutex_init(&b->lock);
  gpio_apply_pinctrl(node);
  if (rpi_fw_available()) rpi_fw_clock_rate(RPI_CLK_CORE, &b->core_hz);
  if (!b->core_hz) b->core_hz = 250000000;
  writel(CS_CLEAR, b->base + SPI_CS);
  b->id = dt_alias_id(node, "spi");
  if (b->id < 0) b->id = next_id++;
  for (int cs = 0; cs < 2; cs++) {
    char name[16];
    snprintf(name, sizeof(name), "spidev%d.%d", b->id, cs);
    dev_t dev = MKDEV(153, b->id * 2 + cs);
    register_chrdev(dev, kstrdup(name, 0), &spidev_fops, b);
    devfs_create(name, S_IFCHR | 0600, dev);
  }
  pr_info("spi%d: BCM2835 SPI at %s (spidev%d.0, spidev%d.1)\n", b->id, fdt_node_name(node), b->id, b->id);
  return 0;
}

DT_DRIVER(bcm2835_spi, DRV_DEVICE, spi_probe, "brcm,bcm2835-spi");
