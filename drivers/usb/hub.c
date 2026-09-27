/*
 * USB hubs (class 9), USB 1.1/2.0 (with transaction translators for
 * low/full-speed devices behind high-speed hubs) and USB 3. The driver
 * powers the downstream ports, then one kernel thread ("hubd") watches
 * every hub: woken by a hub's status-change endpoint, and polling once a
 * second in case a change was missed. New devices are debounced, reset and
 * handed to the host controller (addressing) and the USB core (drivers).
 *
 * On the Raspberry Pi 4 the USB 2.0 ports are behind a VIA hub on the
 * VL805, so keyboards and thumb drives there need this driver.
 */
#include <olux/kernel.h>
#include <olux/list.h>
#include <olux/mm.h>
#include <olux/sched.h>
#include <olux/time.h>
#include <olux/usb.h>
#include <olux/wait.h>

#define HUB_MAX_PORTS 15
#define USB_DT_HUB 0x29
#define USB_DT_SS_HUB 0x2a
#define HUB_SET_DEPTH 12

/* port features */
#define PORT_RESET 4
#define PORT_POWER 8
#define C_PORT_CONNECTION 16
#define C_PORT_ENABLE 17
#define C_PORT_OVER_CURRENT 19
#define C_PORT_RESET 20
#define C_PORT_LINK_STATE 25
#define C_PORT_CONFIG_ERROR 26
#define C_BH_PORT_RESET 29

/* wPortStatus / wPortChange bits */
#define PS_CONNECTION (1u << 0)
#define PS_ENABLE (1u << 1)
#define PS_LOW_SPEED (1u << 9)
#define PS_HIGH_SPEED (1u << 10)
#define PC_CONNECTION (1u << 0)
#define PC_ENABLE (1u << 1)
#define PC_OVER_CURRENT (1u << 3)
#define PC_RESET (1u << 4)
#define PC_BH_RESET (1u << 5)
#define PC_LINK_STATE (1u << 6)
#define PC_CONFIG_ERROR (1u << 7)

struct hub {
  struct usb_interface *intf;
  struct usb_device *dev;
  int nports;
  bool ss;
  struct usb_device *child[HUB_MAX_PORTS + 1];
  bool gone;
  struct list_head link;
};

static LIST_HEAD(hubs);
static DEFINE_WAIT_QUEUE(hubd_wq);
static volatile bool hubd_kick;
static struct thread *hubd_thread;

static int port_feature(struct hub *h, bool set, u16 feature, int port) {
  return usb_control(h->dev, USB_TYPE_CLASS | USB_RECIP_OTHER, set ? USB_REQ_SET_FEATURE : USB_REQ_CLEAR_FEATURE,
                     feature, (u16)port, NULL, 0);
}

static int port_status(struct hub *h, int port, u16 *status, u16 *change) {
  u8 b[4];
  int r = usb_control(h->dev, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER, USB_REQ_GET_STATUS, 0, (u16)port, b, 4);
  if (r < 4) return r < 0 ? r : -EIO;
  *status = (u16)(b[0] | b[1] << 8);
  *change = (u16)(b[2] | b[3] << 8);
  return 0;
}

/* Reset a port; returns the port status afterwards, or -errno. */
static int port_reset(struct hub *h, int port) {
  u16 st, ch;
  if (port_feature(h, true, PORT_RESET, port) < 0) return -EIO;
  u64 end = ktime_ns() + 500 * 1000000ULL;
  for (;;) {
    sleep_ns(10 * 1000000);
    if (port_status(h, port, &st, &ch)) return -EIO;
    if (ch & (PC_RESET | PC_BH_RESET)) break;
    if (ktime_ns() > end) return -ETIMEDOUT;
  }
  port_feature(h, false, C_PORT_RESET, port);
  if (h->ss && (ch & PC_BH_RESET)) port_feature(h, false, C_BH_PORT_RESET, port);
  sleep_ns(10 * 1000000); /* reset recovery */
  if (port_status(h, port, &st, &ch)) return -EIO;
  return st;
}

static void child_remove(struct hub *h, int port) {
  struct usb_device *c = h->child[port];
  if (!c) return;
  h->child[port] = NULL;
  h->dev->ops->detach(c);
}

static void child_add(struct hub *h, int port) {
  u16 st, ch;
  sleep_ns(100 * 1000000); /* debounce */
  if (port_status(h, port, &st, &ch) || !(st & PS_CONNECTION)) return;
  int r = port_reset(h, port);
  if (r < 0 || !(r & PS_ENABLE)) {
    pr_warn("hub %d: port %d: reset failed\n", h->dev->devnum, port);
    return;
  }
  int speed = h->ss                    ? USB_SPEED_SUPER
              : (r & PS_LOW_SPEED)  ? USB_SPEED_LOW
              : (r & PS_HIGH_SPEED) ? USB_SPEED_HIGH
                                    : USB_SPEED_FULL;
  struct usb_device *c = h->dev->ops->attach_child(h->dev, port, speed);
  if (!c) return;
  h->child[port] = c; /* kept even if setup fails, so it is not retried every second */
  if (usb_new_device(c)) pr_warn("hub %d: port %d: device setup failed\n", h->dev->devnum, port);
}

static void port_event(struct hub *h, int port) {
  u16 st, ch;
  if (port_status(h, port, &st, &ch)) return;
  if (ch & PC_CONNECTION) port_feature(h, false, C_PORT_CONNECTION, port);
  if (!h->ss && (ch & PC_ENABLE)) port_feature(h, false, C_PORT_ENABLE, port);
  if (ch & PC_OVER_CURRENT) {
    port_feature(h, false, C_PORT_OVER_CURRENT, port);
    pr_warn("hub %d: port %d: over-current\n", h->dev->devnum, port);
  }
  if (ch & PC_RESET) port_feature(h, false, C_PORT_RESET, port);
  if (h->ss) {
    if (ch & PC_BH_RESET) port_feature(h, false, C_BH_PORT_RESET, port);
    if (ch & PC_LINK_STATE) port_feature(h, false, C_PORT_LINK_STATE, port);
    if (ch & PC_CONFIG_ERROR) port_feature(h, false, C_PORT_CONFIG_ERROR, port);
  }
  bool connected = st & PS_CONNECTION;
  if (h->child[port] && (!connected || (ch & PC_CONNECTION))) child_remove(h, port); /* gone, or replaced */
  if (connected && !h->child[port]) child_add(h, port);
}

static int hubd(void *arg) {
  (void)arg;
  for (;;) {
    wait_event_interruptible_timeout(hubd_wq, hubd_kick, (long)NSEC_PER_SEC);
    hubd_kick = false;
    usb_topology_lock();
    struct hub *h, *n;
    /* hubs found or removed during the pass are marked, not unlinked */
    list_for_each_entry(h, &hubs, link) {
      for (int p = 1; p <= h->nports && !h->gone && !h->dev->gone; p++) port_event(h, p);
    }
    list_for_each_entry_safe(h, n, &hubs, link) {
      if (h->gone) list_del(&h->link); /* not freed: a late status-change callback may still run */
    }
    usb_topology_unlock();
  }
  return 0;
}

static void hub_complete(struct usb_interface *intf, const u8 *data, int len, int status) {
  (void)intf;
  (void)data;
  (void)len;
  if (status) return;
  hubd_kick = true;
  wake_up(&hubd_wq);
}

static int hub_probe(struct usb_interface *intf) {
  struct usb_device *d = intf->dev;
  if (!d->ops->hub_config || !d->ops->attach_child) return -ENODEV;
  if (d->depth >= 5) {
    pr_warn("hub %d: too many tiers of hubs; not used\n", d->devnum);
    return -ENODEV;
  }
  struct hub *h = kzalloc(sizeof(*h), 0);
  if (!h) return -ENOMEM;
  h->intf = intf;
  h->dev = d;
  h->ss = d->speed == USB_SPEED_SUPER;
  u8 desc[12];
  int r = usb_control(d, USB_DIR_IN | USB_TYPE_CLASS, USB_REQ_GET_DESCRIPTOR, (h->ss ? USB_DT_SS_HUB : USB_DT_HUB) << 8,
                      0, desc, sizeof(desc));
  if (r < 7) {
    pr_warn("hub %d: cannot read the hub descriptor (%d)\n", d->devnum, r);
    goto fail;
  }
  h->nports = MIN((int)desc[2], HUB_MAX_PORTS);
  int ttt = (desc[3] >> 5) & 3;
  u32 pgood_ms = desc[5] * 2u;
  if (h->ss && usb_control(d, USB_TYPE_CLASS, HUB_SET_DEPTH, (u16)d->depth, 0, NULL, 0) < 0)
    pr_warn("hub %d: SET_HUB_DEPTH failed\n", d->devnum);
  if (d->ops->hub_config(d, h->nports, ttt)) {
    pr_warn("hub %d: host controller refused the hub\n", d->devnum);
    goto fail;
  }
  for (int p = 1; p <= h->nports; p++) port_feature(h, true, PORT_POWER, p);
  sleep_ns((u64)MAX(pgood_ms, 20u) * 1000000);
  intf->priv = h;
  for (int i = 0; i < intf->nep; i++) /* status changes; without it, the one-second poll still works */
    if (intf->ep[i].type == USB_EP_XFER_INT && (intf->ep[i].addr & USB_DIR_IN)) {
      d->ops->intr_start(intf, intf->ep[i].addr, intf->ep[i].mps, hub_complete);
      break;
    }
  pr_info("hub %d: %d-port %s hub\n", d->devnum, h->nports,
          h->ss ? "USB 3" : d->speed == USB_SPEED_HIGH ? "USB 2.0" : "USB 1.1");
  list_add_tail(&h->link, &hubs);
  if (!hubd_thread) {
    hubd_thread = kthread_create(hubd, NULL, "hubd");
    if (hubd_thread) sched_add_new(hubd_thread);
  }
  hubd_kick = true;
  wake_up(&hubd_wq);
  return 0;
fail:
  kfree(h);
  return -ENODEV;
}

/* The hub is gone (or its parent is): remove everything below it first. */
static void hub_disconnect(struct usb_interface *intf) {
  struct hub *h = intf->priv;
  if (!h) return;
  for (int p = 1; p <= h->nports; p++) child_remove(h, p);
  h->gone = true;
}

USB_DRIVER(hub, .name = "hub", .cls = 9, .subcls = USB_ANY, .proto = USB_ANY, .probe = hub_probe,
           .disconnect = hub_disconnect);
