#ifndef OLUX_VIRTIO_H
#define OLUX_VIRTIO_H

#include <olux/spinlock.h>
#include <olux/types.h>

#define VIRTIO_ID_NET 1
#define VIRTIO_ID_BLOCK 2
#define VIRTIO_ID_CONSOLE 3
#define VIRTIO_ID_RNG 4
#define VIRTIO_ID_GPU 16
#define VIRTIO_ID_INPUT 18

#define VIRTIO_F_VERSION_1 32

struct virtio_device;

struct vq_desc {
  u64 addr;
  u32 len;
  u16 flags;
  u16 next;
};
#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

struct virtqueue {
  struct virtio_device *vdev;
  unsigned index, num;
  struct vq_desc *desc;
  volatile u16 *avail; /* flags, idx, ring[num], used_event */
  volatile u8 *used;   /* flags, idx, ring[num]{id,len}, avail_event */
  phys_addr_t ring_pa;
  u16 free_head, num_free, last_used;
  void **tokens;
  void (*callback)(struct virtqueue *vq);
  spinlock_t lock;
};

/* Scatter-gather element: physical address + length (+ device-writable). */
struct vq_buf {
  phys_addr_t addr;
  u32 len;
  bool write;
};

struct virtio_device {
  u8 *base;
  int version;
  u32 id;
  int irq;
  u64 features;
  struct virtqueue *vqs[4];
  int nvqs;
  void *priv;
};

u32 virtio_config_read32(struct virtio_device *d, unsigned off);
u8 virtio_config_read8(struct virtio_device *d, unsigned off);
u64 virtio_config_read64(struct virtio_device *d, unsigned off);
/* Negotiate features: `wanted` is intersected with the device's offer. */
int virtio_negotiate(struct virtio_device *d, u64 wanted);
struct virtqueue *virtio_setup_vq(struct virtio_device *d, unsigned index, unsigned max, void (*cb)(struct virtqueue *));
int virtio_driver_ok(struct virtio_device *d);
int virtqueue_add(struct virtqueue *vq, const struct vq_buf *bufs, int n, void *token);
void virtqueue_kick(struct virtqueue *vq);
void *virtqueue_get(struct virtqueue *vq, u32 *len);

/* Device drivers (probed by the transport). */
int virtio_blk_probe(struct virtio_device *d);
int virtio_rng_probe(struct virtio_device *d);
int virtio_net_probe(struct virtio_device *d);

#endif
