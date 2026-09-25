/* virtio entropy device: seeds the kernel CSPRNG. */
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/random.h>
#include <olux/time.h>
#include <olux/virtio.h>

int virtio_rng_probe(struct virtio_device *d) {
  if (virtio_negotiate(d, 0)) return -EIO;
  struct virtqueue *vq = virtio_setup_vq(d, 0, 8, NULL);
  if (!vq) return -EIO;
  virtio_driver_ok(d);
  phys_addr_t pa;
  u8 *buf = dma_alloc_coherent(PAGE_SIZE, &pa, GFP_DMA32);
  if (!buf) return -ENOMEM;
  struct vq_buf b = {pa, 64, true};
  virtqueue_add(vq, &b, 1, buf);
  virtqueue_kick(vq);
  /* poll for completion (interrupts are not wired up yet during probe) */
  u32 len = 0;
  for (int i = 0; i < 100000 && !virtqueue_get(vq, &len); i++) udelay(10);
  if (len) {
    add_entropy(buf, len, len * 8);
    pr_info("virtio-rng: seeded CSPRNG with %u bytes\n", len);
  }
  dma_free_coherent(buf, PAGE_SIZE);
  return 0;
}
