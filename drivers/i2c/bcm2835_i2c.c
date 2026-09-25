/*
 * BCM2835/BCM2711 BSC I2C master, exported as /dev/i2c-N with the Linux
 * i2c-dev ABI (I2C_SLAVE, I2C_RDWR, I2C_SMBUS, read/write), so standard
 * tools (i2cdetect, i2cget, i2cset, i2ctransfer) work.
 *
 * Transfers are polled. Messages of an I2C_RDWR request are sent back to
 * back; the controller issues a STOP between them rather than a repeated
 * START, which register-oriented devices accept.
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

#define BSC_C 0x00
#define BSC_S 0x04
#define BSC_DLEN 0x08
#define BSC_A 0x0c
#define BSC_FIFO 0x10
#define BSC_DIV 0x14
#define BSC_CLKT 0x1c

#define C_I2CEN (1u << 15)
#define C_ST (1u << 7)
#define C_CLEAR (3u << 4)
#define C_READ (1u << 0)
#define S_TA (1u << 0)
#define S_DONE (1u << 1)
#define S_TXD (1u << 4)
#define S_RXD (1u << 5)
#define S_ERR (1u << 8)
#define S_CLKT (1u << 9)

/* i2c-dev ABI */
#define I2C_RETRIES 0x0701
#define I2C_TIMEOUT 0x0702
#define I2C_SLAVE 0x0703
#define I2C_TENBIT 0x0704
#define I2C_FUNCS 0x0705
#define I2C_SLAVE_FORCE 0x0706
#define I2C_RDWR 0x0707
#define I2C_PEC 0x0708
#define I2C_SMBUS 0x0720
#define I2C_M_RD 0x0001
#define I2C_FUNC_I2C 0x00000001
#define I2C_FUNC_SMBUS_EMUL 0x0f7f0000 /* quick, byte, byte/word data, block, i2c block */
#define I2C_SMBUS_READ 1
#define I2C_SMBUS_QUICK 0
#define I2C_SMBUS_BYTE 1
#define I2C_SMBUS_BYTE_DATA 2
#define I2C_SMBUS_WORD_DATA 3
#define I2C_SMBUS_BLOCK_DATA 5
#define I2C_SMBUS_I2C_BLOCK_BROKEN 6
#define I2C_SMBUS_I2C_BLOCK_DATA 8
#define I2C_SMBUS_BLOCK_MAX 32
#define I2C_RDWR_MAX_MSGS 42
#define MAX_XFER 8192

struct i2c_msg {
  u16 addr, flags, len;
  u64 buf;
};
struct i2c_rdwr_ioctl_data {
  u64 msgs;
  u32 nmsgs;
};
struct i2c_smbus_ioctl_data {
  u8 read_write, command;
  u32 size;
  u64 data;
};

struct bsc {
  u8 *base;
  struct mutex lock;
  u32 bus_hz;
  int id;
};

struct i2c_client {
  struct bsc *bus;
  u16 addr;
};

/* One message; returns 0, -ENXIO (no ACK), -ETIMEDOUT or -EIO. */
static int bsc_xfer(struct bsc *b, u16 addr, u8 *buf, u32 len, bool rd) {
  if (len > 0xffff) return -EINVAL;
  writel(C_CLEAR, b->base + BSC_C);
  writel(S_CLKT | S_ERR | S_DONE, b->base + BSC_S);
  writel(addr, b->base + BSC_A);
  writel(len, b->base + BSC_DLEN);
  writel(C_I2CEN | C_ST | (rd ? C_READ : 0), b->base + BSC_C);
  u32 pos = 0;
  u64 deadline = ktime_ns() + (20 + (u64)len * 100000000 / b->bus_hz) * NSEC_PER_MSEC / 10;
  for (int spins = 0;; spins++) {
    u32 s = readl(b->base + BSC_S);
    if (rd) {
      while (pos < len && (s & S_RXD)) {
        buf[pos++] = (u8)readl(b->base + BSC_FIFO);
        s = readl(b->base + BSC_S);
      }
    } else {
      while (pos < len && (s & S_TXD)) {
        writel(buf[pos++], b->base + BSC_FIFO);
        s = readl(b->base + BSC_S);
      }
    }
    if (s & (S_ERR | S_CLKT)) {
      writel(S_CLKT | S_ERR | S_DONE, b->base + BSC_S);
      writel(C_CLEAR, b->base + BSC_C);
      return (s & S_ERR) ? -ENXIO : -ETIMEDOUT;
    }
    if (s & S_DONE) {
      while (rd && pos < len && (readl(b->base + BSC_S) & S_RXD)) buf[pos++] = (u8)readl(b->base + BSC_FIFO);
      writel(S_DONE, b->base + BSC_S);
      return pos == len ? 0 : -EIO;
    }
    if (ktime_ns() > deadline) {
      writel(C_CLEAR, b->base + BSC_C);
      return -ETIMEDOUT;
    }
    if (spins > 100) sleep_ns(20 * 1000);
  }
}

static int xfer_locked(struct bsc *b, u16 addr, u8 *buf, u32 len, bool rd) {
  mutex_lock(&b->lock);
  int r = bsc_xfer(b, addr, buf, len, rd);
  mutex_unlock(&b->lock);
  return r;
}

static int i2c_open(struct inode *i, struct file *f) {
  struct i2c_client *c = kzalloc(sizeof(*c), 0);
  if (!c) return -ENOMEM;
  c->bus = f->priv;
  f->priv = c;
  return 0;
}

static int i2c_release(struct inode *i, struct file *f) {
  kfree(f->priv);
  return 0;
}

static ssize_t i2c_rw(struct file *f, struct iobuf *b, bool rd) {
  struct i2c_client *c = f->priv;
  if (!c->addr) return -EDESTADDRREQ;
  size_t len = MIN(b->len, (size_t)MAX_XFER);
  u8 *buf = kmalloc(len ? len : 1, 0);
  if (!buf) return -ENOMEM;
  int r = rd ? 0 : iob_read(b, 0, buf, len);
  if (!r) r = xfer_locked(c->bus, c->addr, buf, len, rd);
  if (!r && rd && iob_write(b, 0, buf, len)) r = -EFAULT;
  kfree(buf);
  return r ? r : (ssize_t)len;
}

static ssize_t i2c_read(struct file *f, struct iobuf *b, loff_t *pos) { return i2c_rw(f, b, true); }
static ssize_t i2c_write(struct file *f, struct iobuf *b, loff_t *pos) { return i2c_rw(f, b, false); }

static int do_rdwr(struct bsc *bus, u64 arg) {
  struct i2c_rdwr_ioctl_data d;
  if (copy_from_user(&d, arg, sizeof(d))) return -EFAULT;
  if (!d.nmsgs || d.nmsgs > I2C_RDWR_MAX_MSGS) return -EINVAL;
  struct i2c_msg msgs[I2C_RDWR_MAX_MSGS];
  if (copy_from_user(msgs, d.msgs, d.nmsgs * sizeof(msgs[0]))) return -EFAULT;
  u8 *buf = kmalloc(MAX_XFER, 0);
  if (!buf) return -ENOMEM;
  int r = 0;
  mutex_lock(&bus->lock);
  for (u32 i = 0; i < d.nmsgs && !r; i++) {
    struct i2c_msg *m = &msgs[i];
    bool rd = m->flags & I2C_M_RD;
    if (m->len > MAX_XFER || m->addr > 0x7f)
      r = -EINVAL;
    else if (!rd && copy_from_user(buf, m->buf, m->len))
      r = -EFAULT;
    if (!r) r = bsc_xfer(bus, m->addr, buf, m->len, rd);
    if (!r && rd && copy_to_user(m->buf, buf, m->len)) r = -EFAULT;
  }
  mutex_unlock(&bus->lock);
  kfree(buf);
  return r ? r : (int)d.nmsgs;
}

static int do_smbus(struct i2c_client *c, u64 arg) {
  struct i2c_smbus_ioctl_data d;
  if (copy_from_user(&d, arg, sizeof(d))) return -EFAULT;
  if (!c->addr && d.size != I2C_SMBUS_QUICK) return -EDESTADDRREQ;
  bool rd = d.read_write == I2C_SMBUS_READ;
  u8 data[I2C_SMBUS_BLOCK_MAX + 2] = {0}; /* union i2c_smbus_data */
  size_t dlen = d.size == I2C_SMBUS_BYTE || d.size == I2C_SMBUS_BYTE_DATA ? 1
                : d.size == I2C_SMBUS_WORD_DATA                           ? 2
                                                                          : sizeof(data);
  if (d.size != I2C_SMBUS_QUICK && !(d.size == I2C_SMBUS_BYTE && !rd) && d.data && copy_from_user(data, d.data, dlen))
    return -EFAULT;
  struct bsc *b = c->bus;
  u8 tx[I2C_SMBUS_BLOCK_MAX + 2];
  int r;
  mutex_lock(&b->lock);
  switch (d.size) {
    case I2C_SMBUS_QUICK:
      r = bsc_xfer(b, c->addr, tx, 0, rd);
      break;
    case I2C_SMBUS_BYTE:
      if (rd) {
        r = bsc_xfer(b, c->addr, data, 1, true);
      } else {
        tx[0] = d.command;
        r = bsc_xfer(b, c->addr, tx, 1, false);
      }
      break;
    case I2C_SMBUS_BYTE_DATA:
    case I2C_SMBUS_WORD_DATA: {
      u32 n = d.size == I2C_SMBUS_BYTE_DATA ? 1 : 2;
      tx[0] = d.command;
      if (rd) {
        r = bsc_xfer(b, c->addr, tx, 1, false);
        if (!r) r = bsc_xfer(b, c->addr, data, n, true);
      } else {
        memcpy(tx + 1, data, n);
        r = bsc_xfer(b, c->addr, tx, 1 + n, false);
      }
      break;
    }
    case I2C_SMBUS_BLOCK_DATA:
    case I2C_SMBUS_I2C_BLOCK_BROKEN:
    case I2C_SMBUS_I2C_BLOCK_DATA: {
      bool counted = d.size == I2C_SMBUS_BLOCK_DATA;
      u32 n = data[0] > I2C_SMBUS_BLOCK_MAX ? I2C_SMBUS_BLOCK_MAX : data[0];
      tx[0] = d.command;
      if (rd) {
        r = bsc_xfer(b, c->addr, tx, 1, false);
        if (!r && counted) { /* count byte, then up to 32 data bytes, in one read */
          r = bsc_xfer(b, c->addr, data, I2C_SMBUS_BLOCK_MAX + 1, true);
          if (data[0] > I2C_SMBUS_BLOCK_MAX) data[0] = I2C_SMBUS_BLOCK_MAX;
        } else if (!r) {
          r = bsc_xfer(b, c->addr, data + 1, n, true);
        }
      } else {
        u32 hdr = counted ? 2 : 1;
        if (counted) tx[1] = (u8)n;
        memcpy(tx + hdr, data + 1, n);
        r = bsc_xfer(b, c->addr, tx, hdr + n, false);
      }
      break;
    }
    default:
      r = -EOPNOTSUPP;
  }
  mutex_unlock(&b->lock);
  if (!r && rd && d.size != I2C_SMBUS_QUICK && copy_to_user(d.data, data, dlen)) r = -EFAULT;
  return r;
}

static long i2c_ioctl(struct file *f, unsigned cmd, u64 arg) {
  struct i2c_client *c = f->priv;
  switch (cmd) {
    case I2C_SLAVE:
    case I2C_SLAVE_FORCE:
      if (arg > 0x7f) return -EINVAL;
      c->addr = (u16)arg;
      return 0;
    case I2C_FUNCS:
      return put_user((unsigned long)(I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL), (unsigned long *)arg);
    case I2C_RDWR:
      return do_rdwr(c->bus, arg);
    case I2C_SMBUS:
      return do_smbus(c, arg);
    case I2C_TENBIT:
      return arg ? -EOPNOTSUPP : 0;
    case I2C_RETRIES:
    case I2C_TIMEOUT:
    case I2C_PEC:
      return 0;
    default:
      return -ENOTTY;
  }
}

static const struct file_operations i2c_fops = {
    .open = i2c_open, .release = i2c_release, .read = i2c_read, .write = i2c_write, .ioctl = i2c_ioctl};

static int bsc_probe(int node) {
  static int next_id = 20; /* for buses without an alias */
  struct bsc *b = kzalloc(sizeof(*b), 0);
  if (!b) return -ENOMEM;
  b->base = dt_ioremap(node, 0, NULL);
  if (!b->base) return -ENOMEM;
  mutex_init(&b->lock);
  gpio_apply_pinctrl(node);
  b->bus_hz = 100000;
  fdt_getprop_u32(node, "clock-frequency", &b->bus_hz);
  if (!b->bus_hz) b->bus_hz = 100000;
  u32 core = 0;
  if (rpi_fw_available()) rpi_fw_clock_rate(RPI_CLK_CORE, &core);
  if (!core) core = 150000000;
  u32 div = (core + b->bus_hz - 1) / b->bus_hz;
  writel(ALIGN_UP(div, 2) & 0xfffe, b->base + BSC_DIV);
  writel(35, b->base + BSC_CLKT); /* clock-stretch timeout, in SCL cycles */
  writel(0, b->base + BSC_C);
  b->id = dt_alias_id(node, "i2c");
  if (b->id < 0) b->id = next_id++;
  char name[16];
  snprintf(name, sizeof(name), "i2c-%d", b->id);
  register_chrdev(MKDEV(89, b->id), kstrdup(name, 0), &i2c_fops, b);
  devfs_create(name, S_IFCHR | 0600, MKDEV(89, b->id));
  pr_info("%s: BSC I2C at %s, %u kHz\n", name, fdt_node_name(node), b->bus_hz / 1000);
  return 0;
}

DT_DRIVER(bcm2835_i2c, DRV_DEVICE, bsc_probe, "brcm,bcm2835-i2c", "brcm,bcm2711-i2c");
