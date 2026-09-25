/* virtio block device (/dev/vdX). One request in flight per disk. */
#include <olux/blkdev.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/time.h>
#include <olux/virtio.h>
#include <olux/wait.h>

#define VIRTIO_BLK_F_RO 5
#define VIRTIO_BLK_F_FLUSH 9
#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_T_FLUSH 4

struct vblk_req {
  u32 type, reserved;
  u64 sector;
};

struct vblk {
  struct blkdev bd;
  struct virtio_device *vdev;
  struct virtqueue *vq;
  struct vblk_req *req; /* DMA-able header + status */
  phys_addr_t req_pa;
  volatile u8 *status;
  struct completion done;
};

static int vblk_submit(struct vblk *v, u32 type, u64 sector, void *buf, u32 bytes, bool write) {
  v->req->type = type;
  v->req->reserved = 0;
  v->req->sector = sector;
  *v->status = 0xff;
  struct vq_buf b[3];
  int n = 0;
  b[n++] = (struct vq_buf){v->req_pa, sizeof(struct vblk_req), false};
  if (bytes) {
    if (write) dcache_clean_range(buf, bytes);
    else dcache_flush_range(buf, bytes);
    b[n++] = (struct vq_buf){virt_to_phys(buf), bytes, !write};
  }
  b[n++] = (struct vq_buf){v->req_pa + sizeof(struct vblk_req), 1, true};
  init_completion(&v->done);
  int r = virtqueue_add(v->vq, b, n, v);
  if (r) return r;
  virtqueue_kick(v->vq);
  if (!wait_for_completion_timeout(&v->done, 30 * (long)NSEC_PER_SEC)) return -ETIMEDOUT;
  if (bytes && !write) dcache_inval_range(buf, bytes);
  return *v->status == 0 ? 0 : -EIO;
}

static int vblk_rw(struct blkdev *bd, u64 sector, void *buf, u32 count, bool write) {
  struct vblk *v = bd->priv;
  if (!is_linear_addr((u64)buf)) return -EINVAL;
  return vblk_submit(v, write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN, sector, buf, count * SECTOR_SIZE, write);
}

static int vblk_flush(struct blkdev *bd) {
  struct vblk *v = bd->priv;
  if (!(v->vdev->features & (1ULL << VIRTIO_BLK_F_FLUSH))) return 0;
  return vblk_submit(v, VIRTIO_BLK_T_FLUSH, 0, NULL, 0, false);
}

static const struct blkdev_ops vblk_ops = {.rw = vblk_rw, .flush = vblk_flush};

static void vblk_done(struct virtqueue *vq) {
  struct vblk *v;
  while ((v = virtqueue_get(vq, NULL))) complete(&v->done);
}

int virtio_blk_probe(struct virtio_device *d) {
  static int ndisks;
  struct vblk *v = kzalloc(sizeof(*v), 0);
  if (!v) return -ENOMEM;
  v->vdev = d;
  if (virtio_negotiate(d, (1ULL << VIRTIO_BLK_F_RO) | (1ULL << VIRTIO_BLK_F_FLUSH))) return -EIO;
  v->vq = virtio_setup_vq(d, 0, 64, vblk_done);
  if (!v->vq) return -EIO;
  v->req = dma_alloc_coherent(PAGE_SIZE, &v->req_pa, GFP_DMA32);
  if (!v->req) return -ENOMEM;
  v->status = (volatile u8 *)(v->req + 1);
  init_completion(&v->done);
  snprintf(v->bd.name, sizeof(v->bd.name), "vd%c", 'a' + ndisks++);
  v->bd.nr_sectors = virtio_config_read64(d, 0);
  v->bd.readonly = d->features & (1ULL << VIRTIO_BLK_F_RO);
  v->bd.ops = &vblk_ops;
  v->bd.priv = v;
  d->priv = v;
  virtio_driver_ok(d); /* partition scanning below issues requests */
  return blkdev_register(&v->bd, 254, "") ? -EIO : 0;
}
