/*
 * virtio-mmio transport (legacy v1 and modern v2) with split virtqueues.
 */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/virtio.h>

#define VIRTIO_MAGIC 0x000
#define VIRTIO_VERSION 0x004
#define VIRTIO_DEVICE_ID 0x008
#define VIRTIO_DEVICE_FEATURES 0x010
#define VIRTIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_DRIVER_FEATURES 0x020
#define VIRTIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_QUEUE_SEL 0x030
#define VIRTIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_QUEUE_NUM 0x038
#define VIRTIO_QUEUE_ALIGN 0x03c
#define VIRTIO_QUEUE_PFN 0x040
#define VIRTIO_QUEUE_READY 0x044
#define VIRTIO_QUEUE_NOTIFY 0x050
#define VIRTIO_INTERRUPT_STATUS 0x060
#define VIRTIO_INTERRUPT_ACK 0x064
#define VIRTIO_STATUS 0x070
#define VIRTIO_QUEUE_DESC_LOW 0x080
#define VIRTIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_QUEUE_USED_LOW 0x0a0
#define VIRTIO_QUEUE_USED_HIGH 0x0a4
#define VIRTIO_CONFIG 0x100

#define STATUS_ACK 1
#define STATUS_DRIVER 2
#define STATUS_DRIVER_OK 4
#define STATUS_FEATURES_OK 8
#define STATUS_FAILED 128

u32 virtio_config_read32(struct virtio_device *d, unsigned off) { return readl(d->base + VIRTIO_CONFIG + off); }
u8 virtio_config_read8(struct virtio_device *d, unsigned off) { return readb(d->base + VIRTIO_CONFIG + off); }
u64 virtio_config_read64(struct virtio_device *d, unsigned off) {
  return virtio_config_read32(d, off) | ((u64)virtio_config_read32(d, off + 4) << 32);
}

int virtio_negotiate(struct virtio_device *d, u64 wanted) {
  writel(0, d->base + VIRTIO_DEVICE_FEATURES_SEL);
  u64 offered = readl(d->base + VIRTIO_DEVICE_FEATURES);
  writel(1, d->base + VIRTIO_DEVICE_FEATURES_SEL);
  offered |= (u64)readl(d->base + VIRTIO_DEVICE_FEATURES) << 32;
  if (d->version >= 2) wanted |= 1ULL << VIRTIO_F_VERSION_1;
  d->features = offered & wanted;
  writel(0, d->base + VIRTIO_DRIVER_FEATURES_SEL);
  writel((u32)d->features, d->base + VIRTIO_DRIVER_FEATURES);
  writel(1, d->base + VIRTIO_DRIVER_FEATURES_SEL);
  writel((u32)(d->features >> 32), d->base + VIRTIO_DRIVER_FEATURES);
  if (d->version >= 2) {
    writel(readl(d->base + VIRTIO_STATUS) | STATUS_FEATURES_OK, d->base + VIRTIO_STATUS);
    if (!(readl(d->base + VIRTIO_STATUS) & STATUS_FEATURES_OK)) return -EIO;
  }
  return 0;
}

struct virtqueue *virtio_setup_vq(struct virtio_device *d, unsigned index, unsigned max, void (*cb)(struct virtqueue *)) {
  writel(index, d->base + VIRTIO_QUEUE_SEL);
  unsigned num = readl(d->base + VIRTIO_QUEUE_NUM_MAX);
  if (!num) return NULL;
  if (num > max) num = max;
  struct virtqueue *vq = kzalloc(sizeof(*vq), 0);
  if (!vq) return NULL;
  vq->vdev = d;
  vq->index = index;
  vq->num = num;
  vq->callback = cb;
  spin_lock_init(&vq->lock);
  vq->tokens = kcalloc(num, sizeof(void *), 0);
  /* legacy layout: desc | avail | pad to 4 KiB | used */
  size_t desc_sz = 16 * num, avail_sz = 6 + 2 * num;
  size_t used_off = ALIGN_UP(desc_sz + avail_sz, PAGE_SIZE);
  size_t total = used_off + ALIGN_UP(6 + 8 * num, PAGE_SIZE);
  u8 *ring = dma_alloc_coherent(total, &vq->ring_pa, GFP_DMA32);
  if (!ring || !vq->tokens) return NULL;
  vq->desc = (struct vq_desc *)ring;
  vq->avail = (volatile u16 *)(ring + desc_sz);
  vq->used = ring + used_off;
  for (unsigned i = 0; i < num; i++) vq->desc[i].next = i + 1;
  vq->free_head = 0;
  vq->num_free = num;
  writel(num, d->base + VIRTIO_QUEUE_NUM);
  if (d->version == 1) {
    writel(PAGE_SIZE, d->base + VIRTIO_GUEST_PAGE_SIZE);
    writel(PAGE_SIZE, d->base + VIRTIO_QUEUE_ALIGN);
    writel((u32)(vq->ring_pa >> PAGE_SHIFT), d->base + VIRTIO_QUEUE_PFN);
  } else {
    phys_addr_t a = vq->ring_pa, av = a + desc_sz, u = a + used_off;
    writel((u32)a, d->base + VIRTIO_QUEUE_DESC_LOW);
    writel((u32)(a >> 32), d->base + VIRTIO_QUEUE_DESC_HIGH);
    writel((u32)av, d->base + VIRTIO_QUEUE_AVAIL_LOW);
    writel((u32)(av >> 32), d->base + VIRTIO_QUEUE_AVAIL_HIGH);
    writel((u32)u, d->base + VIRTIO_QUEUE_USED_LOW);
    writel((u32)(u >> 32), d->base + VIRTIO_QUEUE_USED_HIGH);
    writel(1, d->base + VIRTIO_QUEUE_READY);
  }
  d->vqs[index] = vq;
  if ((int)index >= d->nvqs) d->nvqs = index + 1;
  return vq;
}

int virtio_driver_ok(struct virtio_device *d) {
  writel(readl(d->base + VIRTIO_STATUS) | STATUS_DRIVER_OK, d->base + VIRTIO_STATUS);
  return 0;
}

int virtqueue_add(struct virtqueue *vq, const struct vq_buf *bufs, int n, void *token) {
  unsigned long f = spin_lock_irqsave(&vq->lock);
  if (vq->num_free < n || n == 0) {
    spin_unlock_irqrestore(&vq->lock, f);
    return -ENOSPC;
  }
  u16 head = vq->free_head, idx = head, prev = 0;
  for (int i = 0; i < n; i++) {
    struct vq_desc *dsc = &vq->desc[idx];
    dsc->addr = bufs[i].addr;
    dsc->len = bufs[i].len;
    dsc->flags = (bufs[i].write ? VRING_DESC_F_WRITE : 0) | (i + 1 < n ? VRING_DESC_F_NEXT : 0);
    prev = idx;
    idx = dsc->next;
  }
  vq->free_head = vq->desc[prev].next;
  vq->desc[prev].next = 0;
  vq->num_free -= n;
  vq->tokens[head] = token;
  u16 aidx = vq->avail[1];
  vq->avail[2 + aidx % vq->num] = head;
  dmb(ishst);
  vq->avail[1] = aidx + 1;
  spin_unlock_irqrestore(&vq->lock, f);
  return 0;
}

void virtqueue_kick(struct virtqueue *vq) {
  dsb(sy);
  writel(vq->index, vq->vdev->base + VIRTIO_QUEUE_NOTIFY);
}

void *virtqueue_get(struct virtqueue *vq, u32 *len) {
  unsigned long f = spin_lock_irqsave(&vq->lock);
  volatile u16 *uidx = (volatile u16 *)(vq->used + 2);
  if (vq->last_used == *uidx) {
    spin_unlock_irqrestore(&vq->lock, f);
    return NULL;
  }
  dmb(ishld);
  volatile u32 *elem = (volatile u32 *)(vq->used + 4 + 8 * (vq->last_used % vq->num));
  u32 id = elem[0];
  if (len) *len = elem[1];
  vq->last_used++;
  void *token = vq->tokens[id];
  /* return the descriptor chain to the free list */
  u16 i = id;
  int cnt = 1;
  while (vq->desc[i].flags & VRING_DESC_F_NEXT) {
    i = vq->desc[i].next;
    cnt++;
  }
  vq->desc[i].next = vq->free_head;
  vq->free_head = id;
  vq->num_free += cnt;
  spin_unlock_irqrestore(&vq->lock, f);
  return token;
}

static void virtio_irq(int irq, void *arg) {
  struct virtio_device *d = arg;
  u32 st = readl(d->base + VIRTIO_INTERRUPT_STATUS);
  writel(st, d->base + VIRTIO_INTERRUPT_ACK);
  if (st & 1)
    for (int i = 0; i < d->nvqs; i++)
      if (d->vqs[i] && d->vqs[i]->callback) d->vqs[i]->callback(d->vqs[i]);
}

__weak int virtio_net_probe(struct virtio_device *d) { return -ENODEV; }

static int virtio_mmio_probe(int node) {
  u64 addr, size;
  if (fdt_get_reg(node, 0, &addr, &size)) return -EINVAL;
  u8 *base = ioremap(addr, size);
  if (!base) return -ENOMEM;
  if (readl(base + VIRTIO_MAGIC) != 0x74726976) {
    iounmap(base);
    return -ENODEV;
  }
  u32 id = readl(base + VIRTIO_DEVICE_ID);
  if (!id) {
    iounmap(base); /* empty slot */
    return -ENODEV;
  }
  struct virtio_device *d = kzalloc(sizeof(*d), 0);
  d->base = base;
  d->version = readl(base + VIRTIO_VERSION);
  d->id = id;
  u32 irq, flags;
  if (fdt_get_irq(node, 0, &irq, &flags)) return -EINVAL;
  d->irq = irq;
  writel(0, base + VIRTIO_STATUS); /* reset */
  writel(STATUS_ACK | STATUS_DRIVER, base + VIRTIO_STATUS);
  /* drivers may issue requests (and wait for their interrupts) in probe */
  request_irq(d->irq, virtio_irq, d, "virtio");
  int r;
  switch (id) {
    case VIRTIO_ID_BLOCK:
      r = virtio_blk_probe(d);
      break;
    case VIRTIO_ID_RNG:
      r = virtio_rng_probe(d);
      break;
    case VIRTIO_ID_NET:
      r = virtio_net_probe(d);
      break;
    default:
      r = -ENODEV;
  }
  if (r) {
    free_irq(d->irq);
    writel(STATUS_FAILED, base + VIRTIO_STATUS);
    return r;
  }
  virtio_driver_ok(d);
  return 0;
}

DT_DRIVER(virtio_mmio, DRV_DEVICE, virtio_mmio_probe, "virtio,mmio");
