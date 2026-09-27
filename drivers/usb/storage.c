/*
 * USB mass storage, Bulk-Only Transport with the SCSI transparent command
 * set (class 8, subclass 6, protocol 0x50): thumb drives, card readers and
 * USB disks appear as /dev/sdX with partitions.
 *
 * Errors follow the BOT recovery rules: a stalled data stage is cleared and
 * the status still read; a bad status triggers a Bulk-Only reset and
 * clearing both endpoints; commands that fail are retried.
 */
#include <olux/blkdev.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/sched.h>
#include <olux/usb.h>

#define CBW_SIG 0x43425355u
#define CSW_SIG 0x53425355u
#define BOT_RESET 0xff
#define BOT_GET_MAX_LUN 0xfe
#define MAX_XFER 65536
#define TIMEOUT_MS 10000

struct cbw {
  u32 sig, tag, len;
  u8 flags, lun, cblen;
  u8 cb[16];
} __attribute__((packed));

struct csw {
  u32 sig, tag, residue;
  u8 status;
} __attribute__((packed));

struct msd {
  struct usb_interface *intf;
  u8 ep_in, ep_out;
  u32 tag;
  u32 block_size;
  u64 blocks;
  struct blkdev bd;
  u8 *buf;
};

static void reset_recovery(struct msd *m) {
  struct usb_device *d = m->intf->dev;
  usb_control(d, USB_TYPE_CLASS | USB_RECIP_INTERFACE, BOT_RESET, 0, m->intf->number, NULL, 0);
  d->ops->clear_halt(d, m->ep_in);
  d->ops->clear_halt(d, m->ep_out);
}

/* One SCSI command. data_in: transfer direction. Returns 0, 1 (check
 * condition) or -errno. */
static int scsi(struct msd *m, const u8 *cb, int cblen, void *data, u32 len, bool data_in) {
  struct usb_device *d = m->intf->dev;
  if (d->gone) return -ENODEV;
  struct cbw w = {CBW_SIG, ++m->tag, len, data_in ? 0x80 : 0, 0, (u8)cblen, {0}};
  memcpy(w.cb, cb, (size_t)cblen);
  int r = d->ops->bulk(d, m->ep_out, &w, sizeof(w), TIMEOUT_MS);
  if (r != (int)sizeof(w)) {
    reset_recovery(m);
    return r < 0 ? r : -EIO;
  }
  if (len) {
    r = d->ops->bulk(d, data_in ? m->ep_in : m->ep_out, data, len, TIMEOUT_MS);
    if (r == -EPIPE)
      d->ops->clear_halt(d, data_in ? m->ep_in : m->ep_out);
    else if (r < 0) {
      reset_recovery(m);
      return r;
    }
  }
  struct csw s;
  r = d->ops->bulk(d, m->ep_in, &s, sizeof(s), TIMEOUT_MS);
  if (r == -EPIPE) { /* stalled status: clear and try once more */
    d->ops->clear_halt(d, m->ep_in);
    r = d->ops->bulk(d, m->ep_in, &s, sizeof(s), TIMEOUT_MS);
  }
  if (r != (int)sizeof(s) || s.sig != CSW_SIG || s.tag != w.tag || s.status == 2) {
    reset_recovery(m);
    return r < 0 ? r : -EIO;
  }
  return s.status ? 1 : 0;
}

static void request_sense(struct msd *m, u8 *key, u8 *asc) {
  u8 cb[6] = {0x03, 0, 0, 0, 18, 0}, sense[18] = {0};
  *key = *asc = 0;
  if (scsi(m, cb, 6, sense, sizeof(sense), true) == 0) {
    *key = sense[2] & 0xf;
    *asc = sense[12];
  }
}

static int msd_rw(struct blkdev *bd, u64 sector, void *buf, u32 count, bool write) {
  struct msd *m = bd->priv;
  u32 per = m->block_size / 512; /* 512-byte sectors per device block */
  if (sector % per || count % per) return -EINVAL;
  while (count) {
    u32 n = MIN(count, (u32)(MAX_XFER / 512));
    n -= n % per;
    u32 lba = (u32)(sector / per), blocks = n / per;
    u8 cb[10] = {
        write ? 0x2a : 0x28, 0, (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba, 0, (u8)(blocks >> 8),
        (u8)blocks,          0};
    int r = -EIO;
    for (int attempt = 0; attempt < 3; attempt++) {
      if (write) memcpy(m->buf, buf, (size_t)n * 512);
      r = scsi(m, cb, 10, m->buf, n * 512, !write);
      if (r == 0) break;
      if (r == -ENODEV) return -EIO;
      if (r == 1) { /* check condition: log once, retry (e.g. unit attention) */
        u8 key, asc;
        request_sense(m, &key, &asc);
        if (attempt == 2)
          pr_warn("%s: %s at %u failed, sense %x/%02x\n", bd->name, write ? "write" : "read", lba, key, asc);
      }
    }
    if (r) return -EIO;
    if (!write) memcpy(buf, m->buf, (size_t)n * 512);
    sector += n;
    count -= n;
    buf = (u8 *)buf + (size_t)n * 512;
  }
  return 0;
}

static int msd_flush(struct blkdev *bd) {
  struct msd *m = bd->priv;
  u8 cb[10] = {0x35}; /* SYNCHRONIZE CACHE(10) */
  int r = scsi(m, cb, 10, NULL, 0, false);
  return r < 0 ? r : 0; /* many devices reject it: that is fine */
}

static const struct blkdev_ops msd_ops = {.rw = msd_rw, .flush = msd_flush};

static int msd_probe(struct usb_interface *intf) {
  static int ndisks;
  if (intf->subcls != 6 || intf->proto != 0x50) return -ENODEV; /* SCSI, bulk-only */
  struct msd *m = kzalloc(sizeof(*m), 0);
  if (!m) return -ENOMEM;
  m->intf = intf;
  for (int i = 0; i < intf->nep; i++) {
    if (intf->ep[i].type != USB_EP_XFER_BULK) continue;
    if (intf->ep[i].addr & USB_DIR_IN)
      m->ep_in = intf->ep[i].addr;
    else
      m->ep_out = intf->ep[i].addr;
  }
  m->buf = kmalloc(MAX_XFER, 0);
  if (!m->ep_in || !m->ep_out || !m->buf) goto fail;
  intf->priv = m;

  u8 inq_cb[6] = {0x12, 0, 0, 0, 36, 0}, inq[36] = {0};
  if (scsi(m, inq_cb, 6, inq, sizeof(inq), true) < 0) goto fail;
  if ((inq[0] & 0x1f) != 0x00 && (inq[0] & 0x1f) != 0x0e) goto fail; /* direct access / simplified */
  /* wait for the medium (card readers, spinning disks) */
  int ready = -1;
  for (int i = 0; i < 20 && ready != 0; i++) {
    u8 tur[6] = {0};
    ready = scsi(m, tur, 6, NULL, 0, false);
    if (ready == 1) {
      u8 key, asc;
      request_sense(m, &key, &asc);
      sleep_ns(100 * 1000000);
    } else if (ready < 0) {
      break;
    }
  }
  u8 cap_cb[10] = {0x25}, cap[8];
  if (scsi(m, cap_cb, 10, cap, sizeof(cap), true) != 0) {
    pr_warn("usb-storage: no medium\n");
    goto fail;
  }
  u32 last = (u32)cap[0] << 24 | (u32)cap[1] << 16 | (u32)cap[2] << 8 | cap[3];
  m->block_size = (u32)cap[4] << 24 | (u32)cap[5] << 16 | (u32)cap[6] << 8 | cap[7];
  if (m->block_size < 512 || m->block_size > 4096 || (m->block_size & (m->block_size - 1))) {
    pr_warn("usb-storage: unsupported block size %u\n", m->block_size);
    goto fail;
  }
  m->blocks = (u64)last + 1;
  char vendor[9], model[17];
  memcpy(vendor, inq + 8, 8);
  vendor[8] = 0;
  memcpy(model, inq + 16, 16);
  model[16] = 0;
  snprintf(m->bd.name, sizeof(m->bd.name), "sd%c", 'a' + ndisks++);
  m->bd.nr_sectors = m->blocks * (m->block_size / 512);
  m->bd.ops = &msd_ops;
  m->bd.priv = m;
  pr_info("%s: USB mass storage '%s %s', %u-byte blocks\n", m->bd.name, vendor, model, m->block_size);
  return blkdev_register(&m->bd, 8, "") ? -EIO : 0;
fail:
  intf->priv = NULL;
  kfree(m->buf);
  kfree(m);
  return -ENODEV;
}

static void msd_disconnect(struct usb_interface *intf) {
  struct msd *m = intf->priv;
  if (m) pr_info("%s: removed; further I/O fails\n", m->bd.name);
}

USB_DRIVER(usb_storage, .name = "usb-storage", .cls = 8, .subcls = USB_ANY, .proto = USB_ANY, .probe = msd_probe,
           .disconnect = msd_disconnect);
