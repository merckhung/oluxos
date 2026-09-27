/*
 * xHCI USB host controller (PCI class 0c0330): QEMU qemu-xhci, the VIA
 * VL805 on the Raspberry Pi 4, and other xHCI 1.x controllers.
 *
 * One command ring, one event ring (interrupter 0) and a 256-TRB transfer
 * ring per endpoint, all in uncached DMA memory. Events are consumed in the
 * interrupt handler, which completes waiters and runs interrupt-endpoint
 * callbacks; a kernel thread ("usbd") handles port connects and
 * disconnects. Transfers use a per-device 64 KiB bounce buffer.
 */
#include <olux/device.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/pci.h>
#include <olux/sched.h>
#include <olux/spinlock.h>
#include <olux/time.h>
#include <olux/usb.h>
#include <olux/wait.h>

/* capability registers */
#define CAPLENGTH 0x00
#define HCSPARAMS1 0x04
#define HCSPARAMS2 0x08
#define HCCPARAMS1 0x10
#define DBOFF 0x14
#define RTSOFF 0x18
/* operational registers */
#define USBCMD 0x00
#define USBSTS 0x04
#define PAGESIZE 0x08
#define CRCR 0x18
#define DCBAAP 0x30
#define CONFIG 0x38
#define PORTSC(p) (0x400 + 0x10 * ((p) - 1))
#define CMD_RUN (1u << 0)
#define CMD_HCRST (1u << 1)
#define CMD_INTE (1u << 2)
#define STS_HCH (1u << 0)
#define STS_EINT (1u << 3)
#define STS_CNR (1u << 11)
/* PORTSC */
#define PS_CCS (1u << 0)
#define PS_PED (1u << 1)
#define PS_PR (1u << 4)
#define PS_PP (1u << 9)
#define PS_SPEED(x) (((x) >> 10) & 0xf)
#define PS_CHANGES (0x7fu << 17) /* CSC PEC WRC OCC PRC PLC CEC: write 1 to clear */
#define PS_CSC (1u << 17)
#define PS_PRC (1u << 21)
#define PS_PRESERVE (PS_PP | (0xfu << 14) /* PIC, LWS off */ | (7u << 25) /* wake bits */)
/* runtime registers (interrupter 0) */
#define IR0 0x20
#define IMAN 0x00
#define IMOD 0x04
#define ERSTSZ 0x08
#define ERSTBA 0x10
#define ERDP 0x18
#define IMAN_IP 1u
#define IMAN_IE 2u
#define ERDP_EHB (1u << 3)

/* TRBs */
#define TRB_NORMAL 1
#define TRB_SETUP 2
#define TRB_DATA 3
#define TRB_STATUS 4
#define TRB_LINK 6
#define TRB_ENABLE_SLOT 9
#define TRB_DISABLE_SLOT 10
#define TRB_ADDRESS_DEVICE 11
#define TRB_CONFIGURE_EP 12
#define TRB_EVALUATE_CTX 13
#define TRB_RESET_EP 14
#define TRB_SET_TR_DEQUEUE 16
#define EV_TRANSFER 32
#define EV_COMMAND 33
#define EV_PORT 34
#define C_CYCLE (1u << 0)
#define C_TC (1u << 1) /* link: toggle cycle */
#define C_ISP (1u << 2)
#define C_IOC (1u << 5)
#define C_IDT (1u << 6)
#define C_TYPE(t) ((u32)(t) << 10)
#define C_DIR_IN (1u << 16)
#define GET_TYPE(c) (((c) >> 10) & 0x3f)
#define CC_SUCCESS 1
#define CC_STALL 6
#define CC_SHORT 13

#define RING_TRBS 256
#define BOUNCE 65536
#define CMD_TIMEOUT_NS (1000L * 1000 * 1000)

struct trb {
  u64 param;
  u32 status;
  u32 control;
};

struct ring {
  struct trb *t;
  phys_addr_t pa;
  unsigned enq;
  u32 cycle;
};

struct xhci;

struct xep {
  struct ring ring;
  bool active;
  /* synchronous transfer in progress */
  volatile bool done;
  volatile u32 cc;
  volatile u32 residual;
  u64 wait_trb; /* bus address of the TRB whose event completes the transfer */
  /* interrupt IN polling */
  usb_complete_t cb;
  struct usb_interface *cb_intf;
  u8 *ibuf;
  phys_addr_t ibuf_pa;
  u16 ilen;
};

struct xdev {
  struct usb_device udev;
  struct xhci *x;
  int slot, port;
  void *in_ctx, *out_ctx;
  phys_addr_t in_pa, out_pa;
  struct xep ep[32]; /* by device context index */
  u8 *bounce;
  phys_addr_t bounce_pa;
  struct mutex lock; /* one synchronous transfer at a time */
  struct wait_queue wq;
  u32 route;           /* route string: hub ports below the root port */
  u8 tt_slot, tt_port; /* transaction translator of a low/full-speed device behind a high-speed hub */
};

struct xhci {
  struct pci_dev *pci;
  u8 *cap, *op, *rt;
  u32 *db;
  unsigned max_slots, max_ports, ctxsz;
  u64 *dcbaa;
  phys_addr_t dcbaa_pa;
  struct ring cmd, evt;
  struct trb *erst_seg;
  u64 *erst;
  phys_addr_t erst_pa;
  unsigned evt_deq;
  u32 evt_cycle;
  spinlock_t lock;
  struct mutex cmd_lock;
  struct wait_queue cmd_wq;
  volatile bool cmd_done;
  volatile u32 cmd_cc, cmd_slot;
  u64 cmd_trb;
  struct xdev *slots[256];
  struct xdev *ports[256];
  volatile u32 port_events[8];
  struct wait_queue port_wq;
  u8 port_major[256]; /* USB major revision per port (2 or 3) */
};

static u32 rd(u8 *base, u32 off) { return readl(base + off); }
static void wr(u8 *base, u32 off, u32 v) { writel(v, base + off); }
static void wr64(u8 *base, u32 off, u64 v) {
  writel((u32)v, base + off);
  writel((u32)(v >> 32), base + off + 4);
}

static u64 bus(struct xhci *x, phys_addr_t pa) { return pci_bus_addr(x->pci, pa); }

/* Below 1 GiB where such memory exists: the BCM2711 PCIe inbound window
 * covers only the low 3 GiB. QEMU virt has no RAM there, so fall back. */
static void *dma_page(struct xhci *x, phys_addr_t *pa, size_t size) {
  void *p = dma_alloc_coherent(size, pa, GFP_DMA);
  return p ? p : dma_alloc_coherent(size, pa, GFP_DMA32);
}

static int ring_init(struct xhci *x, struct ring *r) {
  r->t = dma_page(x, &r->pa, RING_TRBS * sizeof(struct trb));
  if (!r->t) return -ENOMEM;
  r->enq = 0;
  r->cycle = 1;
  struct trb *link = &r->t[RING_TRBS - 1];
  link->param = bus(x, r->pa);
  link->status = 0;
  link->control = C_TYPE(TRB_LINK) | C_TC; /* cycle bit set when handed over */
  return 0;
}

/* Queue a TRB; returns its bus address. */
static u64 ring_push(struct xhci *x, struct ring *r, u64 param, u32 status, u32 control) {
  struct trb *t = &r->t[r->enq];
  u64 addr = bus(x, r->pa + r->enq * sizeof(struct trb));
  t->param = param;
  t->status = status;
  __asm__ volatile("dmb oshst" ::: "memory");
  t->control = (control & ~C_CYCLE) | r->cycle;
  if (++r->enq == RING_TRBS - 1) { /* hand the link TRB over and wrap */
    struct trb *link = &r->t[RING_TRBS - 1];
    __asm__ volatile("dmb oshst" ::: "memory");
    link->control = (link->control & ~C_CYCLE) | r->cycle;
    r->enq = 0;
    r->cycle ^= 1;
  }
  return addr;
}

static u64 ring_enq_addr(struct xhci *x, struct ring *r) { return bus(x, r->pa + r->enq * sizeof(struct trb)); }

static void doorbell(struct xhci *x, unsigned slot, u32 target) {
  __asm__ volatile("dsb sy" ::: "memory");
  writel(target, &x->db[slot]);
}

static void *ctx(struct xhci *x, void *base, int idx) { return (u8 *)base + idx * x->ctxsz; }

/* ---------------- events (interrupt context) ---------------- */

static void handle_transfer(struct xhci *x, struct trb *e) {
  unsigned slot = e->control >> 24, dci = (e->control >> 16) & 0x1f;
  struct xdev *d = slot < 256 ? x->slots[slot] : NULL;
  if (!d || !dci) return;
  struct xep *ep = &d->ep[dci];
  u32 cc = e->status >> 24, residual = e->status & 0xffffff;
  if (ep->cb) { /* interrupt IN polling */
    if (cc == CC_SUCCESS || cc == CC_SHORT) ep->cb(ep->cb_intf, ep->ibuf, ep->ilen - (int)residual, 0);
    if (!d->udev.gone && (cc == CC_SUCCESS || cc == CC_SHORT)) {
      ring_push(x, &ep->ring, bus(x, ep->ibuf_pa), ep->ilen, C_TYPE(TRB_NORMAL) | C_IOC | C_ISP);
      doorbell(x, slot, dci);
    } else if (cc != CC_SUCCESS && cc != CC_SHORT) {
      ep->cb(ep->cb_intf, NULL, 0, -EIO);
    }
    return;
  }
  if (e->param == ep->wait_trb || (cc != CC_SUCCESS && cc != CC_SHORT)) {
    if (cc == CC_SHORT || ep->residual == 0) ep->residual = residual;
    ep->cc = cc;
    ep->done = true;
    wake_up(&d->wq);
  } else if (cc == CC_SHORT) { /* short data stage of a control transfer */
    ep->residual = residual;
    ep->cc = cc;
  }
}

static void xhci_irq(int irq, void *arg) {
  struct xhci *x = arg;
  u32 sts = rd(x->op, USBSTS);
  if (!(sts & STS_EINT) && !(rd(x->rt, IR0 + IMAN) & IMAN_IP)) return; /* shared line: not ours */
  wr(x->op, USBSTS, STS_EINT);
  wr(x->rt, IR0 + IMAN, IMAN_IP | IMAN_IE);
  unsigned long f = spin_lock_irqsave(&x->lock);
  for (;;) {
    struct trb *e = &x->erst_seg[x->evt_deq];
    if ((e->control & C_CYCLE) != x->evt_cycle) break;
    __asm__ volatile("dmb oshld" ::: "memory");
    switch (GET_TYPE(e->control)) {
      case EV_TRANSFER:
        handle_transfer(x, e);
        break;
      case EV_COMMAND:
        if (e->param == x->cmd_trb) {
          x->cmd_cc = e->status >> 24;
          x->cmd_slot = e->control >> 24;
          x->cmd_done = true;
          wake_up(&x->cmd_wq);
        }
        break;
      case EV_PORT: {
        unsigned port = (unsigned)(e->param >> 24) & 0xff;
        if (port && port <= x->max_ports) x->port_events[port / 32] |= 1u << (port % 32);
        wake_up(&x->port_wq);
        break;
      }
    }
    if (++x->evt_deq == RING_TRBS) {
      x->evt_deq = 0;
      x->evt_cycle ^= 1;
    }
  }
  wr64(x->rt, IR0 + ERDP, bus(x, x->erst_pa + 64 + x->evt_deq * sizeof(struct trb)) | ERDP_EHB);
  spin_unlock_irqrestore(&x->lock, f);
}

/* ---------------- commands ---------------- */

static int command(struct xhci *x, u64 param, u32 status, u32 control, u32 *slot_out) {
  mutex_lock(&x->cmd_lock);
  unsigned long f = spin_lock_irqsave(&x->lock);
  x->cmd_done = false;
  x->cmd_trb = ring_push(x, &x->cmd, param, status, control);
  spin_unlock_irqrestore(&x->lock, f);
  doorbell(x, 0, 0);
  long w = wait_event_interruptible_timeout(x->cmd_wq, x->cmd_done, CMD_TIMEOUT_NS);
  int r = !x->cmd_done ? -ETIMEDOUT : x->cmd_cc == CC_SUCCESS ? 0 : -EIO;
  if (!x->cmd_done && w != 0) r = -EINTR;
  if (r == -EIO) pr_warn("xhci: command %u failed, completion code %u\n", GET_TYPE(control), x->cmd_cc);
  if (slot_out) *slot_out = x->cmd_slot;
  mutex_unlock(&x->cmd_lock);
  return r;
}

/* Recover a halted endpoint: reset it and move its dequeue pointer past the failed TD. */
static void reset_endpoint(struct xdev *d, unsigned dci) {
  struct xhci *x = d->x;
  struct xep *ep = &d->ep[dci];
  command(x, 0, 0, C_TYPE(TRB_RESET_EP) | (u32)dci << 16 | (u32)d->slot << 24, NULL);
  command(x, ring_enq_addr(x, &ep->ring) | ep->ring.cycle, 0,
          C_TYPE(TRB_SET_TR_DEQUEUE) | (u32)dci << 16 | (u32)d->slot << 24, NULL);
}

/* ---------------- transfers ---------------- */

static int wait_xfer(struct xdev *d, unsigned dci, int timeout_ms) {
  struct xep *ep = &d->ep[dci];
  long w = wait_event_interruptible_timeout(d->wq, ep->done || d->udev.gone, (long)timeout_ms * 1000000);
  if (d->udev.gone) return -ENODEV;
  if (!ep->done) {
    reset_endpoint(d, dci); /* stops the ring after the abandoned TD */
    return w == -ERESTARTSYS ? -EINTR : -ETIMEDOUT;
  }
  if (ep->cc == CC_STALL) {
    reset_endpoint(d, dci);
    return -EPIPE;
  }
  if (ep->cc != CC_SUCCESS && ep->cc != CC_SHORT) {
    reset_endpoint(d, dci);
    return -EIO;
  }
  return 0;
}

static void begin_xfer(struct xep *ep) {
  ep->done = false;
  ep->cc = 0;
  ep->residual = 0;
}

static int xhci_control(struct usb_device *ud, u8 reqtype, u8 req, u16 value, u16 index, void *data, u16 len,
                        int timeout_ms) {
  struct xdev *d = ud->hcpriv;
  struct xhci *x = d->x;
  mutex_lock(&d->lock); /* len (u16) always fits the bounce buffer */
  bool in = reqtype & USB_DIR_IN;
  if (!in && len) memcpy(d->bounce, data, len);
  struct xep *ep = &d->ep[1];
  begin_xfer(ep);
  unsigned long f = spin_lock_irqsave(&x->lock);
  u64 setup = reqtype | (u64)req << 8 | (u64)value << 16 | (u64)index << 32 | (u64)len << 48;
  u32 trt = len ? (in ? 3u : 2u) : 0u;
  ring_push(x, &ep->ring, setup, 8, C_TYPE(TRB_SETUP) | C_IDT | trt << 16);
  if (len) ring_push(x, &ep->ring, bus(x, d->bounce_pa), len, C_TYPE(TRB_DATA) | C_ISP | (in ? C_DIR_IN : 0));
  ep->wait_trb = ring_push(x, &ep->ring, 0, 0, C_TYPE(TRB_STATUS) | C_IOC | (len && in ? 0 : C_DIR_IN));
  spin_unlock_irqrestore(&x->lock, f);
  doorbell(x, (unsigned)d->slot, 1);
  int r = wait_xfer(d, 1, timeout_ms);
  if (!r) {
    r = len - (int)MIN(ep->residual, (u32)len);
    if (in && r > 0) memcpy(data, d->bounce, (size_t)r);
  }
  mutex_unlock(&d->lock);
  return r;
}

static unsigned ep_dci(u8 addr) { return (addr & 0xf) * 2 + ((addr & USB_DIR_IN) ? 1 : 0); }

static int xhci_bulk(struct usb_device *ud, u8 epaddr, void *data, u32 len, int timeout_ms) {
  struct xdev *d = ud->hcpriv;
  struct xhci *x = d->x;
  unsigned dci = ep_dci(epaddr);
  struct xep *ep = &d->ep[dci];
  if (!ep->active || len > BOUNCE) return -EINVAL;
  bool in = epaddr & USB_DIR_IN;
  mutex_lock(&d->lock);
  if (!in) memcpy(d->bounce, data, len);
  begin_xfer(ep);
  unsigned long f = spin_lock_irqsave(&x->lock);
  ep->wait_trb = ring_push(x, &ep->ring, bus(x, d->bounce_pa), len, C_TYPE(TRB_NORMAL) | C_IOC | C_ISP);
  spin_unlock_irqrestore(&x->lock, f);
  doorbell(x, (unsigned)d->slot, dci);
  int r = wait_xfer(d, dci, timeout_ms);
  if (!r) {
    r = (int)len - (int)MIN(ep->residual, len);
    if (in && r > 0) memcpy(data, d->bounce, (size_t)r);
  }
  mutex_unlock(&d->lock);
  return r;
}

static int xhci_intr_start(struct usb_interface *intf, u8 epaddr, u16 len, usb_complete_t done) {
  struct xdev *d = intf->dev->hcpriv;
  struct xhci *x = d->x;
  unsigned dci = ep_dci(epaddr);
  struct xep *ep = &d->ep[dci];
  if (!ep->active || !(epaddr & USB_DIR_IN) || len > 1024) return -EINVAL;
  ep->ibuf = dma_page(x, &ep->ibuf_pa, 4096);
  if (!ep->ibuf) return -ENOMEM;
  ep->ilen = len;
  ep->cb_intf = intf;
  unsigned long f = spin_lock_irqsave(&x->lock);
  ep->cb = done;
  ring_push(x, &ep->ring, bus(x, ep->ibuf_pa), len, C_TYPE(TRB_NORMAL) | C_IOC | C_ISP);
  spin_unlock_irqrestore(&x->lock, f);
  doorbell(x, (unsigned)d->slot, dci);
  return 0;
}

static int xhci_clear_halt(struct usb_device *ud, u8 epaddr) {
  struct xdev *d = ud->hcpriv;
  unsigned dci = ep_dci(epaddr);
  reset_endpoint(d, dci);
  return usb_control(ud, USB_RECIP_ENDPOINT, USB_REQ_CLEAR_FEATURE, USB_ENDPOINT_HALT, epaddr, NULL, 0) < 0 ? -EIO : 0;
}

/* ---------------- device setup ---------------- */

static void fill_ep_ctx(struct xhci *x, struct xdev *d, u32 *c, const struct usb_endpoint *e) {
  static const u8 in_types[4] = {4, 5, 6, 7}, out_types[4] = {4, 1, 2, 3};
  unsigned dci = ep_dci(e->addr);
  u32 type = (e->addr & USB_DIR_IN) ? in_types[e->type] : out_types[e->type];
  u32 interval = 0;
  if (e->type == USB_EP_XFER_INT || e->type == USB_EP_XFER_ISOC) {
    if (d->udev.speed == USB_SPEED_HIGH || d->udev.speed == USB_SPEED_SUPER) {
      interval = e->interval ? e->interval - 1u : 0;
    } else { /* frames (1 ms) -> 125 us units, log2 */
      u32 frames = e->interval ? e->interval : 1, units = frames * 8;
      while ((1u << (interval + 1)) <= units && interval < 10) interval++;
      if (e->type == USB_EP_XFER_ISOC) interval = e->interval ? e->interval - 1u + 3 : 3;
    }
  }
  c[0] = interval << 16;
  c[1] = 3u << 1 | type << 3 | (u32)e->max_burst << 8 | (u32)e->mps << 16;
  u64 deq = bus(x, d->ep[dci].ring.pa) | 1;
  c[2] = (u32)deq;
  c[3] = (u32)(deq >> 32);
  c[4] = e->type == USB_EP_XFER_INT ? (u32)e->mps << 16 | e->mps : 1024;
}

static int xhci_configure(struct usb_device *ud) {
  struct xdev *d = ud->hcpriv;
  struct xhci *x = d->x;
  memset(d->in_ctx, 0, 33 * x->ctxsz);
  u32 *icc = ctx(x, d->in_ctx, 0);
  u32 *slot = ctx(x, d->in_ctx, 1);
  memcpy(slot, ctx(x, d->out_ctx, 0), x->ctxsz);
  unsigned max_dci = 1;
  icc[1] = 1; /* slot context */
  for (int i = 0; i < ud->nintf; i++)
    for (int k = 0; k < ud->intf[i].nep; k++) {
      const struct usb_endpoint *e = &ud->intf[i].ep[k];
      if (e->type == USB_EP_XFER_ISOC) continue; /* isochronous endpoints are not supported */
      unsigned dci = ep_dci(e->addr);
      if (dci < 2) continue;
      if (!d->ep[dci].ring.t && ring_init(x, &d->ep[dci].ring)) return -ENOMEM;
      fill_ep_ctx(x, d, ctx(x, d->in_ctx, (int)dci + 1), e);
      d->ep[dci].active = true;
      icc[1] |= 1u << dci;
      if (dci > max_dci) max_dci = dci;
    }
  slot[0] = (slot[0] & ~(0x1fu << 27)) | max_dci << 27;
  return command(x, bus(x, d->in_pa), 0, C_TYPE(TRB_CONFIGURE_EP) | (u32)d->slot << 24, NULL);
}

static const struct usb_hc_ops xhci_ops;

static void free_xdev(struct xdev *d) {
  for (int i = 0; i < 32; i++) {
    if (d->ep[i].ring.t) dma_free_coherent(d->ep[i].ring.t, RING_TRBS * sizeof(struct trb));
    if (d->ep[i].ibuf) dma_free_coherent(d->ep[i].ibuf, 4096);
  }
  if (d->in_ctx) dma_free_coherent(d->in_ctx, 4096);
  if (d->out_ctx) dma_free_coherent(d->out_ctx, 4096);
  if (d->bounce) dma_free_coherent(d->bounce, BOUNCE);
  kfree(d);
}

static int port_reset(struct xhci *x, unsigned port) {
  u32 ps = rd(x->op, PORTSC(port));
  wr(x->op, PORTSC(port), (ps & PS_PRESERVE) | PS_PR);
  u64 end = ktime_ns() + 500 * 1000000ULL;
  while (!(rd(x->op, PORTSC(port)) & PS_PRC)) {
    if (ktime_ns() > end) return -ETIMEDOUT;
    sleep_ns(1000000);
  }
  ps = rd(x->op, PORTSC(port));
  wr(x->op, PORTSC(port), (ps & PS_PRESERVE) | PS_PRC);
  sleep_ns(20 * 1000000); /* reset recovery */
  return (rd(x->op, PORTSC(port)) & PS_PED) ? 0 : -EIO;
}

/* Give a device on a root port (parent NULL) or behind a hub port an
 * address with endpoint 0 configured. The caller then runs usb_new_device. */
static struct xdev *attach(struct xhci *x, unsigned root_port, int speed, struct xdev *parent, int hub_port) {
  u32 slot;
  if (command(x, 0, 0, C_TYPE(TRB_ENABLE_SLOT), &slot) || !slot || slot > x->max_slots) {
    pr_warn("xhci: port %u: no device slot\n", root_port);
    return NULL;
  }
  struct xdev *d = kzalloc(sizeof(*d), 0);
  if (!d) goto fail_nodev;
  d->x = x;
  d->slot = (int)slot;
  d->port = (int)root_port;
  mutex_init(&d->lock);
  wq_init(&d->wq);
  if (parent) {
    int depth = parent->udev.depth;
    if (depth >= 5) goto fail; /* the route string has five tiers */
    d->route = parent->route | (u32)MIN(hub_port, 15) << (4 * depth);
    d->udev.parent = &parent->udev;
    d->udev.depth = depth + 1;
    if (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL) {
      if (parent->udev.speed == USB_SPEED_HIGH) {
        d->tt_slot = (u8)parent->slot;
        d->tt_port = (u8)hub_port;
      } else {
        d->tt_slot = parent->tt_slot;
        d->tt_port = parent->tt_port;
      }
    }
  }
  d->in_ctx = dma_page(x, &d->in_pa, 4096);
  d->out_ctx = dma_page(x, &d->out_pa, 4096);
  d->bounce = dma_page(x, &d->bounce_pa, BOUNCE);
  if (!d->in_ctx || !d->out_ctx || !d->bounce || ring_init(x, &d->ep[1].ring)) goto fail;
  d->ep[1].active = true;
  x->dcbaa[slot] = bus(x, d->out_pa);
  u16 mps0 = speed == USB_SPEED_SUPER ? 512 : speed == USB_SPEED_HIGH ? 64 : 8;
  memset(d->in_ctx, 0, 33 * x->ctxsz);
  u32 *icc = ctx(x, d->in_ctx, 0), *sc = ctx(x, d->in_ctx, 1), *ep0 = ctx(x, d->in_ctx, 2);
  icc[1] = 3; /* slot + EP0 */
  sc[0] = 1u << 27 | (u32)speed << 20 | d->route;
  sc[1] = root_port << 16;
  sc[2] = d->tt_slot | (u32)d->tt_port << 8;
  ep0[1] = 3u << 1 | 4u << 3 | (u32)mps0 << 16; /* CErr 3, control */
  u64 deq = bus(x, d->ep[1].ring.pa) | 1;
  ep0[2] = (u32)deq;
  ep0[3] = (u32)(deq >> 32);
  ep0[4] = 8;
  x->slots[slot] = d;
  if (command(x, bus(x, d->in_pa), 0, C_TYPE(TRB_ADDRESS_DEVICE) | slot << 24, NULL)) {
    pr_warn("xhci: port %u: address device failed\n", root_port);
    x->slots[slot] = NULL;
    x->dcbaa[slot] = 0;
    goto fail;
  }
  d->udev.ops = &xhci_ops;
  d->udev.hcpriv = d;
  d->udev.port = parent ? hub_port : (int)root_port;
  d->udev.speed = speed;
  if (speed == USB_SPEED_FULL || speed == USB_SPEED_LOW) { /* learn bMaxPacketSize0 */
    u8 dd[8];
    if (xhci_control(&d->udev, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_DEVICE << 8, 0, dd, 8, 1000) == 8 && dd[7] &&
        dd[7] != mps0) {
      memset(d->in_ctx, 0, 33 * x->ctxsz);
      icc[1] = 2;
      memcpy(ep0, ctx(x, d->out_ctx, 1), x->ctxsz);
      ep0[1] = (ep0[1] & 0xffff) | (u32)dd[7] << 16;
      command(x, bus(x, d->in_pa), 0, C_TYPE(TRB_EVALUATE_CTX) | slot << 24, NULL);
    }
  }
  return d;
fail:
  free_xdev(d);
fail_nodev:
  command(x, 0, 0, C_TYPE(TRB_DISABLE_SLOT) | slot << 24, NULL);
  return NULL;
}

static void detach(struct xdev *d) {
  struct xhci *x = d->x;
  d->udev.gone = true;
  wake_up(&d->wq);
  usb_disconnect(&d->udev); /* a hub removes its children here */
  unsigned long f = spin_lock_irqsave(&x->lock);
  x->slots[d->slot] = NULL;
  spin_unlock_irqrestore(&x->lock, f);
  command(x, 0, 0, C_TYPE(TRB_DISABLE_SLOT) | (u32)d->slot << 24, NULL);
  x->dcbaa[d->slot] = 0;
  /* the device structure stays allocated: class drivers may still hold it */
}

static void port_connect(struct xhci *x, unsigned port) {
  if (x->ports[port]) return;
  sleep_ns(100 * 1000000); /* debounce, power good */
  u32 ps = rd(x->op, PORTSC(port));
  if (!(ps & PS_CCS)) return;
  if (!(ps & PS_PED) && port_reset(x, port)) {
    pr_warn("xhci: port %u: reset failed\n", port);
    return;
  }
  ps = rd(x->op, PORTSC(port));
  struct xdev *d = attach(x, port, (int)PS_SPEED(ps), NULL, 0);
  if (!d) return;
  x->ports[port] = d;
  if (usb_new_device(&d->udev)) pr_warn("xhci: port %u: device setup failed\n", port);
}

static void port_disconnect(struct xhci *x, unsigned port) {
  struct xdev *d = x->ports[port];
  if (!d) return;
  x->ports[port] = NULL;
  detach(d);
}

/* ---------------- hubs ---------------- */

static int xhci_hub_config(struct usb_device *ud, int nports, int ttt) {
  struct xdev *d = ud->hcpriv;
  struct xhci *x = d->x;
  memset(d->in_ctx, 0, 33 * x->ctxsz);
  u32 *icc = ctx(x, d->in_ctx, 0), *slot = ctx(x, d->in_ctx, 1);
  memcpy(slot, ctx(x, d->out_ctx, 0), x->ctxsz);
  icc[1] = 1; /* slot context only */
  slot[0] |= 1u << 26;                                   /* Hub */
  slot[1] = (slot[1] & 0x00ffffffu) | (u32)nports << 24; /* Number of Ports */
  if (ud->speed == USB_SPEED_HIGH) slot[2] = (slot[2] & ~(3u << 16)) | (u32)(ttt & 3) << 16;
  return command(x, bus(x, d->in_pa), 0, C_TYPE(TRB_CONFIGURE_EP) | (u32)d->slot << 24, NULL);
}

static struct usb_device *xhci_attach_child(struct usb_device *hub, int port, int speed) {
  struct xdev *h = hub->hcpriv;
  struct xdev *d = attach(h->x, (unsigned)h->port, speed, h, port);
  return d ? &d->udev : NULL;
}

static void xhci_detach(struct usb_device *ud) { detach(ud->hcpriv); }

static const struct usb_hc_ops xhci_ops = {.control = xhci_control,
                                           .bulk = xhci_bulk,
                                           .intr_start = xhci_intr_start,
                                           .configure = xhci_configure,
                                           .clear_halt = xhci_clear_halt,
                                           .hub_config = xhci_hub_config,
                                           .attach_child = xhci_attach_child,
                                           .detach = xhci_detach};

static int usbd(void *arg) {
  struct xhci *x = arg;
  for (unsigned p = 1; p <= x->max_ports; p++)
    if (rd(x->op, PORTSC(p)) & PS_CCS) x->port_events[p / 32] |= 1u << (p % 32);
  for (;;) {
    wait_event_interruptible_timeout(x->port_wq,
                                     x->port_events[0] | x->port_events[1] | x->port_events[2] | x->port_events[3] |
                                         x->port_events[4] | x->port_events[5] | x->port_events[6] | x->port_events[7],
                                     2 * (long)NSEC_PER_SEC);
    for (unsigned p = 1; p <= x->max_ports; p++) {
      if (!(x->port_events[p / 32] & (1u << (p % 32)))) continue;
      __atomic_fetch_and(&x->port_events[p / 32], ~(1u << (p % 32)), __ATOMIC_SEQ_CST);
      u32 ps = rd(x->op, PORTSC(p));
      wr(x->op, PORTSC(p), (ps & PS_PRESERVE) | (ps & PS_CHANGES)); /* acknowledge */
      usb_topology_lock();
      if ((ps & PS_CCS) && !x->ports[p])
        port_connect(x, p);
      else if (!(ps & PS_CCS) && x->ports[p])
        port_disconnect(x, p);
      usb_topology_unlock();
    }
  }
  return 0;
}

/* ---------------- controller setup ---------------- */

static void legacy_handoff(struct xhci *x) {
  u32 xecp = (rd(x->cap, HCCPARAMS1) >> 16) << 2;
  for (int guard = 0; xecp && guard < 64; guard++) {
    u32 v = rd(x->cap, xecp);
    u8 id = v & 0xff;
    if (id == 1) { /* USB legacy support: take ownership from the firmware */
      wr(x->cap, xecp, v | 1u << 24);
      for (int i = 0; i < 100 && (rd(x->cap, xecp) & (1u << 16)); i++) udelay(1000);
      wr(x->cap, xecp + 4, rd(x->cap, xecp + 4) & ~0xe01fu); /* SMI enables off */
    } else if (id == 2) {                                    /* supported protocol: which ports are USB 3 */
      u8 major = v >> 24;
      u32 range = rd(x->cap, xecp + 8);
      unsigned first = range & 0xff, count = (range >> 8) & 0xff;
      for (unsigned p = first; p < first + count && p < 256; p++) x->port_major[p] = major;
    }
    u32 next = (v >> 8) & 0xff;
    xecp = next ? xecp + next * 4 : 0;
  }
}

static int xhci_probe(struct pci_dev *pci) {
  struct xhci *x = kzalloc(sizeof(*x), 0);
  if (!x) return -ENOMEM;
  x->pci = pci;
  pci_set_master(pci);
  x->cap = pci_map_bar(pci, 0);
  if (!x->cap) return -ENOMEM;
  x->op = x->cap + (rd(x->cap, CAPLENGTH) & 0xff);
  x->rt = x->cap + (rd(x->cap, RTSOFF) & ~0x1fu);
  x->db = (u32 *)(x->cap + (rd(x->cap, DBOFF) & ~3u));
  u32 hcs1 = rd(x->cap, HCSPARAMS1), hcs2 = rd(x->cap, HCSPARAMS2), hcc1 = rd(x->cap, HCCPARAMS1);
  x->max_slots = MIN(hcs1 & 0xff, 255u);
  x->max_ports = (hcs1 >> 24) & 0xff;
  x->ctxsz = (hcc1 & 4) ? 64 : 32;
  spin_lock_init(&x->lock);
  mutex_init(&x->cmd_lock);
  wq_init(&x->cmd_wq);
  wq_init(&x->port_wq);
  legacy_handoff(x);

  /* halt and reset */
  wr(x->op, USBCMD, rd(x->op, USBCMD) & ~CMD_RUN);
  for (int i = 0; i < 100 && !(rd(x->op, USBSTS) & STS_HCH); i++) udelay(1000);
  wr(x->op, USBCMD, CMD_HCRST);
  for (int i = 0; i < 1000 && (rd(x->op, USBCMD) & CMD_HCRST); i++) udelay(1000);
  for (int i = 0; i < 1000 && (rd(x->op, USBSTS) & STS_CNR); i++) udelay(1000);
  if (rd(x->op, USBSTS) & STS_CNR) {
    pr_warn("xhci: controller not ready after reset\n");
    return -EIO;
  }
  wr(x->op, CONFIG, x->max_slots);

  x->dcbaa = dma_page(x, &x->dcbaa_pa, 4096);
  if (!x->dcbaa || ring_init(x, &x->cmd)) return -ENOMEM;
  u32 nsp = ((hcs2 >> 27) & 0x1f) | ((hcs2 >> 21) & 0x1f) << 5;
  if (nsp) { /* scratchpad buffers for the controller */
    phys_addr_t arr_pa;
    u64 *arr = dma_page(x, &arr_pa, 4096);
    if (!arr || nsp > 512) return -ENOMEM;
    for (u32 i = 0; i < nsp; i++) {
      phys_addr_t pa;
      if (!dma_page(x, &pa, 4096)) return -ENOMEM;
      arr[i] = bus(x, pa);
    }
    x->dcbaa[0] = bus(x, arr_pa);
  }
  wr64(x->op, DCBAAP, bus(x, x->dcbaa_pa));
  wr64(x->op, CRCR, bus(x, x->cmd.pa) | 1);

  /* event ring: ERST (one entry) and its segment share one allocation */
  x->erst = dma_page(x, &x->erst_pa, 64 + RING_TRBS * sizeof(struct trb));
  if (!x->erst) return -ENOMEM;
  x->erst_seg = (struct trb *)((u8 *)x->erst + 64);
  x->erst[0] = bus(x, x->erst_pa + 64);
  x->erst[1] = RING_TRBS;
  x->evt_cycle = 1;
  wr(x->rt, IR0 + ERSTSZ, 1);
  wr64(x->rt, IR0 + ERDP, bus(x, x->erst_pa + 64));
  wr64(x->rt, IR0 + ERSTBA, bus(x, x->erst_pa));
  wr(x->rt, IR0 + IMOD, 160); /* 40 us moderation */
  wr(x->rt, IR0 + IMAN, IMAN_IP | IMAN_IE);

  if (pci->irq < 0 || request_irq(pci->irq, xhci_irq, x, "xhci")) {
    pr_warn("xhci: no interrupt\n");
    return -EIO;
  }
  pci_write16(pci, PCI_COMMAND, pci_read16(pci, PCI_COMMAND) & ~PCI_COMMAND_INTX_DISABLE);
  wr(x->op, USBCMD, CMD_RUN | CMD_INTE);
  for (int i = 0; i < 100 && (rd(x->op, USBSTS) & STS_HCH); i++) udelay(1000);
  for (unsigned p = 1; p <= x->max_ports; p++) { /* power the ports */
    u32 ps = rd(x->op, PORTSC(p));
    if (!(ps & PS_PP)) wr(x->op, PORTSC(p), (ps & PS_PRESERVE) | PS_PP);
  }
  pr_info("xhci %02x:%02x.%u: xHCI %x.%02x, %u ports, %u slots, IRQ %d\n", pci->bus, pci->dev, pci->fn,
          rd(x->cap, CAPLENGTH) >> 24, (rd(x->cap, CAPLENGTH) >> 16) & 0xff, x->max_ports, x->max_slots, pci->irq);
  struct thread *t = kthread_create(usbd, x, "usbd");
  if (t) sched_add_new(t);
  return 0;
}

PCI_DRIVER(xhci, .name = "xhci", .vendor = PCI_ANY, .device = PCI_ANY, .class = 0x0c0330, .class_mask = 0xffffff,
           .probe = xhci_probe);
