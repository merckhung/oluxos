/*
 * USB core: reads a new device's descriptors, selects its first
 * configuration, has the host controller set up the endpoints and binds
 * interface drivers (HID, mass storage, ...).
 */
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/usb.h>
#include <olux/wait.h>

extern const struct usb_driver __start_usb_drivers[], __stop_usb_drivers[];
static int next_devnum = 1;
static DEFINE_MUTEX(topology_lock);

void usb_topology_lock(void) { mutex_lock(&topology_lock); }
void usb_topology_unlock(void) { mutex_unlock(&topology_lock); }

const char *usb_speed_name(int speed) {
  switch (speed) {
    case USB_SPEED_LOW:
      return "low-speed";
    case USB_SPEED_FULL:
      return "full-speed";
    case USB_SPEED_HIGH:
      return "high-speed";
    case USB_SPEED_SUPER:
      return "SuperSpeed";
    default:
      return "unknown-speed";
  }
}

int usb_control(struct usb_device *d, u8 reqtype, u8 req, u16 value, u16 index, void *data, u16 len) {
  if (d->gone) return -ENODEV;
  return d->ops->control(d, reqtype, req, value, index, data, len, 2000);
}

static void get_string(struct usb_device *d, u8 idx, char *out, size_t size) {
  out[0] = 0;
  if (!idx) return;
  u8 buf[255];
  u16 lang = 0x0409;
  if (usb_control(d, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_STRING << 8, 0, buf, 4) >= 4)
    lang = (u16)(buf[2] | buf[3] << 8);
  int n = usb_control(d, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_STRING << 8 | idx, lang, buf, sizeof(buf));
  if (n < 2 || buf[1] != USB_DT_STRING) return;
  n = MIN(n, (int)buf[0]);
  size_t o = 0;
  for (int i = 2; i + 1 < n && o + 1 < size; i += 2) {
    u16 c = (u16)(buf[i] | buf[i + 1] << 8);
    out[o++] = c >= 0x20 && c < 0x7f ? (char)c : '?';
  }
  out[o] = 0;
}

static int parse_config(struct usb_device *d, const u8 *p, int len) {
  struct usb_interface *cur = NULL;
  d->nintf = 0;
  for (int off = 0; off + 2 <= len;) {
    u8 dl = p[off], dt = p[off + 1];
    if (dl < 2 || off + dl > len) break;
    const u8 *x = p + off;
    if (dt == USB_DT_INTERFACE && dl >= 9) {
      if (x[3] != 0) { /* alternate settings other than 0 are ignored */
        cur = NULL;
      } else if (d->nintf < USB_MAX_INTERFACES) {
        cur = &d->intf[d->nintf++];
        memset(cur, 0, sizeof(*cur));
        cur->dev = d;
        cur->number = x[2];
        cur->cls = x[5];
        cur->subcls = x[6];
        cur->proto = x[7];
      }
    } else if (dt == USB_DT_ENDPOINT && dl >= 7 && cur && cur->nep < USB_MAX_EP) {
      struct usb_endpoint *e = &cur->ep[cur->nep++];
      e->addr = x[2];
      e->type = x[3] & 3;
      e->mps = (u16)((x[4] | x[5] << 8) & 0x7ff);
      e->interval = x[6];
    } else if (dt == USB_DT_SS_EP_COMP && dl >= 6 && cur && cur->nep) {
      cur->ep[cur->nep - 1].max_burst = x[2];
    }
    off += dl;
  }
  return 0;
}

int usb_new_device(struct usb_device *d) {
  u8 dd[18];
  int r = usb_control(d, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_DEVICE << 8, 0, dd, sizeof(dd));
  if (r < 18) {
    pr_warn("usb: port %d: cannot read the device descriptor (%d)\n", d->port, r);
    return r < 0 ? r : -EIO;
  }
  d->devnum = next_devnum++;
  d->cls = dd[4];
  d->subcls = dd[5];
  d->proto = dd[6];
  d->mps0 = dd[7];
  d->vendor = (u16)(dd[8] | dd[9] << 8);
  d->product = (u16)(dd[10] | dd[11] << 8);
  get_string(d, dd[14], d->manufacturer, sizeof(d->manufacturer));
  get_string(d, dd[15], d->product_name, sizeof(d->product_name));
  get_string(d, dd[16], d->serial, sizeof(d->serial));

  u8 hdr[9];
  r = usb_control(d, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_CONFIG << 8, 0, hdr, sizeof(hdr));
  if (r < 9) return -EIO;
  u16 total = (u16)(hdr[2] | hdr[3] << 8);
  if (total < 9 || total > 4096) return -EIO;
  u8 *cfg = kmalloc(total, 0);
  if (!cfg) return -ENOMEM;
  r = usb_control(d, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_CONFIG << 8, 0, cfg, total);
  if (r < 9) {
    kfree(cfg);
    return -EIO;
  }
  parse_config(d, cfg, r);
  d->config_value = cfg[5];
  kfree(cfg);
  char where[24];
  if (d->parent)
    snprintf(where, sizeof(where), "hub %d port %d", d->parent->devnum, d->port);
  else
    snprintf(where, sizeof(where), "port %d", d->port);
  pr_info("usb %d: %04x:%04x %s %s (%s, %s, %d interface%s)\n", d->devnum, d->vendor, d->product,
          d->manufacturer[0] ? d->manufacturer : "-", d->product_name[0] ? d->product_name : "-",
          usb_speed_name(d->speed), where, d->nintf, d->nintf == 1 ? "" : "s");
  r = usb_control(d, 0, USB_REQ_SET_CONFIGURATION, d->config_value, 0, NULL, 0);
  if (r < 0) return r;
  r = d->ops->configure(d);
  if (r) {
    pr_warn("usb %d: endpoint configuration failed (%d)\n", d->devnum, r);
    return r;
  }
  for (int i = 0; i < d->nintf; i++) {
    struct usb_interface *intf = &d->intf[i];
    // cppcheck-suppress comparePointers ; linker-section bounds
    for (const struct usb_driver *drv = __start_usb_drivers; drv < __stop_usb_drivers; drv++) {
      if ((drv->cls != USB_ANY && drv->cls != intf->cls) || (drv->subcls != USB_ANY && drv->subcls != intf->subcls) ||
          (drv->proto != USB_ANY && drv->proto != intf->proto))
        continue;
      if (drv->probe(intf) == 0) {
        intf->driver = drv;
        break;
      }
    }
  }
  return 0;
}

void usb_disconnect(struct usb_device *d) {
  d->gone = true;
  for (int i = 0; i < d->nintf; i++) {
    struct usb_interface *intf = &d->intf[i];
    if (intf->driver && intf->driver->disconnect) intf->driver->disconnect(intf);
    intf->driver = NULL;
  }
  pr_info("usb %d: disconnected\n", d->devnum);
}
