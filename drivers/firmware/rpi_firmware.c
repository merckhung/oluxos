/*
 * BCM2711 VideoCore mailbox and the firmware property interface.
 *
 * The property buffer lives in uncached (Normal-NC) DMA memory below 1 GiB,
 * so no cache maintenance is needed; barriers order the buffer writes
 * against the doorbell. Requests are serialised by a mutex and time out
 * after one second, and every tag's response code is checked.
 *
 * /dev/vcio accepts the Linux IOCTL_MBOX_PROPERTY ioctl, so Raspberry Pi
 * tools that talk to the firmware directly work unchanged.
 */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/rpi_firmware.h>
#include <olux/sched.h>
#include <olux/time.h>
#include <olux/uaccess.h>
#include <olux/wait.h>

#define MBOX_READ 0x00
#define MBOX_STATUS0 0x18
#define MBOX_WRITE 0x20
#define MBOX_STATUS1 0x38
#define MBOX_FULL 0x80000000u
#define MBOX_EMPTY 0x40000000u
#define MBOX_CHAN_PROPERTY 8

#define RESP_SUCCESS 0x80000000u
#define BUF_SIZE 4096
#define TIMEOUT_NS (1000L * 1000 * 1000)

#define IOCTL_MBOX_PROPERTY 0xC0086400u /* _IOWR(100, 0, char *) */

static struct {
  u8 *base;
  int node;
  u32 *buf;
  phys_addr_t buf_pa;
  struct mutex lock;
  bool ok;
} mb;

bool rpi_fw_available(void) { return mb.ok; }

u32 rpi_fw_bus_addr(phys_addr_t pa) { return (u32)dt_dma_addr(mb.node, pa); }

/* Send the message in mb.buf and wait for the answer. Caller holds the lock. */
static int mbox_call(void) {
  u32 msg = (rpi_fw_bus_addr(mb.buf_pa) & ~0xFu) | MBOX_CHAN_PROPERTY;
  u64 deadline = ktime_ns() + TIMEOUT_NS;
  /* drain stale replies (e.g. of a request that timed out) */
  while (!(readl(mb.base + MBOX_STATUS0) & MBOX_EMPTY)) (void)readl(mb.base + MBOX_READ);
  while (readl(mb.base + MBOX_STATUS1) & MBOX_FULL) {
    if (ktime_ns() > deadline) return -ETIMEDOUT;
    udelay(1);
  }
  __asm__ volatile("dsb sy" ::: "memory"); /* buffer visible before the doorbell */
  writel(msg, mb.base + MBOX_WRITE);
  for (;;) {
    while (readl(mb.base + MBOX_STATUS0) & MBOX_EMPTY) {
      if (ktime_ns() > deadline) {
        pr_warn("rpi-fw: mailbox request timed out\n");
        return -ETIMEDOUT;
      }
      udelay(1);
    }
    if (readl(mb.base + MBOX_READ) == msg) break;
  }
  __asm__ volatile("dsb sy" ::: "memory");
  return mb.buf[1] == RESP_SUCCESS ? 0 : -EIO;
}

int rpi_fw_message(void *msg, u32 len) {
  if (!mb.ok) return -ENODEV;
  if (len < 12 || len > BUF_SIZE || (len & 3)) return -EINVAL;
  mutex_lock(&mb.lock);
  memcpy(mb.buf, msg, len);
  mb.buf[0] = len;
  int r = mbox_call();
  memcpy(msg, mb.buf, len);
  mutex_unlock(&mb.lock);
  return r;
}

int rpi_fw_property(u32 tag, void *data, u32 req_len, u32 buf_len) {
  if (!mb.ok) return -ENODEV;
  u32 vlen = ALIGN_UP(MAX(req_len, buf_len), 4);
  u32 total = 6 * 4 + vlen;
  if (total > BUF_SIZE) return -EINVAL;
  mutex_lock(&mb.lock);
  u32 *b = mb.buf;
  b[0] = total;
  b[1] = 0;
  b[2] = tag;
  b[3] = vlen;
  b[4] = req_len;
  memset(&b[5], 0, vlen);
  memcpy(&b[5], data, req_len);
  b[5 + vlen / 4] = 0; /* end tag */
  int r = mbox_call();
  if (!r) {
    if (!(b[4] & RESP_SUCCESS)) {
      r = -EIO;
    } else {
      u32 rlen = b[4] & ~RESP_SUCCESS;
      memcpy(data, &b[5], MIN(rlen, buf_len));
      r = (int)rlen;
    }
  }
  mutex_unlock(&mb.lock);
  return r;
}

int rpi_fw_get_u32(u32 tag, u32 arg, u32 *out) {
  u32 v[2] = {arg, 0};
  int r = rpi_fw_property(tag, v, arg ? 4 : 0, 8);
  if (r < 0) return r;
  *out = arg ? v[1] : v[0];
  return 0;
}

int rpi_fw_clock_rate(u32 clock, u32 *hz) { return rpi_fw_get_u32(RPI_FW_GET_CLOCK_RATE, clock, hz); }

int rpi_fw_set_power(u32 device, bool on) {
  u32 v[2] = {device, on ? 3u : 2u}; /* on + wait for stable */
  int r = rpi_fw_property(RPI_FW_SET_POWER_STATE, v, 8, 8);
  if (r < 0) return r;
  return ((v[1] & 1) == (on ? 1u : 0u)) && !(v[1] & 2) ? 0 : -EIO;
}

/* ---------------- /dev/vcio ---------------- */

static long vcio_ioctl(struct file *f, unsigned cmd, u64 arg) {
  if (cmd != IOCTL_MBOX_PROPERTY) return -ENOTTY;
  u32 size;
  if (copy_from_user(&size, arg, 4)) return -EFAULT;
  if (size < 12 || size > BUF_SIZE || (size & 3)) return -EINVAL;
  u32 *tmp = kmalloc(size, 0);
  if (!tmp) return -ENOMEM;
  int r = copy_from_user(tmp, arg, size);
  if (!r) {
    r = rpi_fw_message(tmp, size);
    if (r == -EIO) r = 0; /* the caller inspects the response code */
    if (!r && copy_to_user(arg, tmp, size)) r = -EFAULT;
  }
  kfree(tmp);
  return r;
}

static const struct file_operations vcio_fops = {.ioctl = vcio_ioctl};

/* ---------------- probe ---------------- */

static const char *board_name(u32 rev) {
  if (!(rev & (1u << 23))) return "old-style revision";
  switch ((rev >> 4) & 0xFF) {
    case 0x11:
      return "Raspberry Pi 4 Model B";
    case 0x13:
      return "Raspberry Pi 400";
    case 0x14:
      return "Raspberry Pi Compute Module 4";
    case 0x17:
      return "Raspberry Pi 5";
    default:
      return "Raspberry Pi";
  }
}

static int rpi_mbox_probe(int node) {
  mb.base = dt_ioremap(node, 0, NULL);
  if (!mb.base) return -ENOMEM;
  mb.node = node;
  mb.buf = dma_alloc_coherent(BUF_SIZE, &mb.buf_pa, GFP_DMA);
  if (!mb.buf) return -ENOMEM;
  mutex_init(&mb.lock);
  mb.ok = true;
  u32 fw = 0, rev = 0, mem[2] = {0, 0}, temp = 0, arm = 0;
  if (rpi_fw_get_u32(RPI_FW_GET_FIRMWARE_REVISION, 0, &fw)) {
    pr_warn("rpi-fw: firmware does not answer; property interface disabled\n");
    mb.ok = false;
    return -EIO;
  }
  rpi_fw_get_u32(RPI_FW_GET_BOARD_REVISION, 0, &rev);
  rpi_fw_property(RPI_FW_GET_ARM_MEMORY, mem, 0, 8);
  u32 tv[2] = {0, 0};
  if (rpi_fw_property(RPI_FW_GET_TEMPERATURE, tv, 4, 8) >= 0) temp = tv[1];
  rpi_fw_clock_rate(RPI_CLK_ARM, &arm);
  u32 memsz = (rev >> 20) & 7;
  pr_info("rpi-fw: firmware %#x, %s rev 1.%u (%u MiB), ARM %u MHz, %u.%u C\n", fw, board_name(rev), rev & 0xF,
          (rev & (1u << 23)) ? 256u << memsz : 0, arm / 1000000, temp / 1000, temp % 1000 / 100);
  register_chrdev(MKDEV(100, 0), "vcio", &vcio_fops, NULL);
  devfs_create("vcio", S_IFCHR | 0600, MKDEV(100, 0));
  return 0;
}

DT_DRIVER(rpi_mbox, DRV_FIRMWARE, rpi_mbox_probe, "brcm,bcm2835-mbox");
