/*
 * virtio network device (QEMU -device virtio-net-device). Receive buffers
 * are posted up front and refilled from the netd thread; transmit copies
 * each frame into one of a pool of buffers.
 */
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/netdev.h>
#include <olux/virtio.h>

#define VIRTIO_NET_F_MAC 5
#define VIRTIO_NET_F_STATUS 16
#define VIRTIO_NET_S_LINK_UP 1

#define NRX 128
#define NTX 64
#define BUFSZ 2048

struct vnet {
  struct virtio_device *vdev;
  struct virtqueue *rx, *tx;
  u8 *rxbuf, *txbuf;
  phys_addr_t rxpa, txpa;
  unsigned hdrlen;
  bool txfree[NTX];
  struct net_device nd;
};

static int post_rx(struct vnet *v, uintptr_t i) {
  struct vq_buf b = {v->rxpa + i * BUFSZ, BUFSZ, true};
  return virtqueue_add(v->rx, &b, 1, (void *)(i + 1));
}

static void reclaim_tx(struct vnet *v) {
  void *tok;
  while ((tok = virtqueue_get(v->tx, NULL))) v->txfree[(uintptr_t)tok - 1] = true;
}

static int vnet_xmit(struct net_device *nd, const void *frame, size_t len) {
  struct vnet *v = nd->priv;
  if (len + v->hdrlen > BUFSZ) return -EMSGSIZE;
  reclaim_tx(v);
  int slot = -1;
  for (int i = 0; i < NTX; i++)
    if (v->txfree[i]) {
      slot = i;
      break;
    }
  if (slot < 0) return -EBUSY;
  u8 *b = v->txbuf + (size_t)slot * BUFSZ;
  memset(b, 0, v->hdrlen); /* no offloads */
  memcpy(b + v->hdrlen, frame, len);
  struct vq_buf vb = {v->txpa + (size_t)slot * BUFSZ, (u32)(len + v->hdrlen), false};
  if (virtqueue_add(v->tx, &vb, 1, (void *)(uintptr_t)(slot + 1))) return -EBUSY;
  v->txfree[slot] = false;
  virtqueue_kick(v->tx);
  return 0;
}

static int vnet_poll(struct net_device *nd, int budget) {
  struct vnet *v = nd->priv;
  int n = 0;
  u32 len;
  void *tok;
  while (n < budget && (tok = virtqueue_get(v->rx, &len))) {
    uintptr_t i = (uintptr_t)tok - 1;
    if (len > v->hdrlen)
      netdev_rx(nd, v->rxbuf + i * BUFSZ + v->hdrlen, len - v->hdrlen);
    else
      nd->rx_errors++;
    post_rx(v, i);
    n++;
  }
  if (n) virtqueue_kick(v->rx);
  return n;
}

static void vnet_rx_irq(struct virtqueue *vq) {
  struct vnet *v = vq->vdev->priv;
  netdev_schedule(&v->nd);
}

static const struct net_device_ops vnet_ops = {.xmit = vnet_xmit, .poll = vnet_poll};

int virtio_net_probe(struct virtio_device *d) {
  struct vnet *v = kzalloc(sizeof(*v), 0);
  if (!v) return -ENOMEM;
  v->vdev = d;
  d->priv = v;
  if (virtio_negotiate(d, (1ULL << VIRTIO_NET_F_MAC) | (1ULL << VIRTIO_NET_F_STATUS))) return -EIO;
  v->hdrlen = (d->features & (1ULL << VIRTIO_F_VERSION_1)) ? 12 : 10;
  v->rx = virtio_setup_vq(d, 0, NRX, vnet_rx_irq);
  v->tx = virtio_setup_vq(d, 1, NTX, NULL);
  if (!v->rx || !v->tx) return -EIO;
  v->rxbuf = dma_alloc_coherent((size_t)NRX * BUFSZ, &v->rxpa, GFP_DMA32);
  v->txbuf = dma_alloc_coherent((size_t)NTX * BUFSZ, &v->txpa, GFP_DMA32);
  if (!v->rxbuf || !v->txbuf) return -ENOMEM;
  for (int i = 0; i < NTX; i++) v->txfree[i] = true;
  for (unsigned i = 0; i < v->rx->num && i < NRX; i++) post_rx(v, i);
  if (d->features & (1ULL << VIRTIO_NET_F_MAC)) {
    for (int i = 0; i < 6; i++) v->nd.mac[i] = virtio_config_read8(d, i);
  } else {
    v->nd.mac[0] = 0x02; /* locally administered */
    for (int i = 1; i < 6; i++) v->nd.mac[i] = (u8)(0x10 + i);
  }
  v->nd.link_up = true;
  if (d->features & (1ULL << VIRTIO_NET_F_STATUS))
    v->nd.link_up = (virtio_config_read8(d, 6) & VIRTIO_NET_S_LINK_UP) != 0;
  v->nd.mtu = 1500;
  v->nd.ops = &vnet_ops;
  v->nd.priv = v;
  virtio_driver_ok(d);
  virtqueue_kick(v->rx);
  return netdev_register(&v->nd);
}
