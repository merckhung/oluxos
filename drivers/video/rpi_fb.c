/*
 * Raspberry Pi firmware framebuffer: allocated through the mailbox property
 * interface, exported as /dev/fb0 with the Linux fbdev ABI (screen info
 * ioctls, read/write/lseek, and mmap as uncached device memory).
 */
#include <asm/pgtable.h>
#include <olux/device.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/rpi_firmware.h>
#include <olux/uaccess.h>
#include <olux/vm.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPAN_DISPLAY 0x4606
#define FBIOBLANK 0x4611

#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR 2

struct fb_bitfield {
  u32 offset, length, msb_right;
};

struct fb_var_screeninfo {
  u32 xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale;
  struct fb_bitfield red, green, blue, transp;
  u32 nonstd, activate, height, width, accel_flags, pixclock, left_margin, right_margin, upper_margin, lower_margin,
      hsync_len, vsync_len, sync, vmode, rotate, colorspace;
  u32 reserved[4];
};

struct fb_fix_screeninfo {
  char id[16];
  u64 smem_start;
  u32 smem_len, type, type_aux, visual;
  u16 xpanstep, ypanstep, ywrapstep;
  u32 line_length;
  u64 mmio_start;
  u32 mmio_len, accel;
  u16 capabilities;
  u16 reserved[2];
};

static struct {
  phys_addr_t phys;
  u8 *virt;
  u32 size, width, height, pitch, depth, yoffset;
} fb;

static void fill_var(struct fb_var_screeninfo *v) {
  memset(v, 0, sizeof(*v));
  v->xres = v->xres_virtual = fb.width;
  v->yres = fb.height;
  v->yres_virtual = fb.size / fb.pitch;
  v->yoffset = fb.yoffset;
  v->bits_per_pixel = fb.depth;
  if (fb.depth == 32) {
    v->red = (struct fb_bitfield){16, 8, 0};
    v->green = (struct fb_bitfield){8, 8, 0};
    v->blue = (struct fb_bitfield){0, 8, 0};
    v->transp = (struct fb_bitfield){24, 8, 0};
  } else {
    v->red = (struct fb_bitfield){11, 5, 0};
    v->green = (struct fb_bitfield){5, 6, 0};
    v->blue = (struct fb_bitfield){0, 5, 0};
  }
  v->height = v->width = ~0u;
}

static long fb_ioctl(struct file *f, unsigned cmd, u64 arg) {
  switch (cmd) {
    case FBIOGET_VSCREENINFO: {
      struct fb_var_screeninfo v;
      fill_var(&v);
      return copy_to_user(arg, &v, sizeof(v));
    }
    case FBIOPUT_VSCREENINFO: /* mode changes are not supported: report the current one */
    case FBIOPAN_DISPLAY: {
      struct fb_var_screeninfo v;
      if (copy_from_user(&v, arg, sizeof(v))) return -EFAULT;
      if (cmd == FBIOPAN_DISPLAY) {
        if (v.yoffset + fb.height > fb.size / fb.pitch) return -EINVAL;
        u32 off[2] = {0, v.yoffset};
        if (rpi_fw_property(RPI_FW_FB_SET_VIRTUAL_OFFSET, off, 8, 8) < 0) return -EIO;
        fb.yoffset = v.yoffset;
      }
      fill_var(&v);
      return copy_to_user(arg, &v, sizeof(v));
    }
    case FBIOGET_FSCREENINFO: {
      struct fb_fix_screeninfo x;
      memset(&x, 0, sizeof(x));
      strlcpy(x.id, "BCM2708 FB", sizeof(x.id));
      x.smem_start = fb.phys;
      x.smem_len = fb.size;
      x.type = FB_TYPE_PACKED_PIXELS;
      x.visual = FB_VISUAL_TRUECOLOR;
      x.ypanstep = 1;
      x.line_length = fb.pitch;
      return copy_to_user(arg, &x, sizeof(x));
    }
    case FBIOBLANK:
      return 0;
    default:
      return -ENOTTY;
  }
}

static ssize_t fb_read(struct file *f, struct iobuf *b, loff_t *pos) {
  if ((u64)*pos >= fb.size) return 0;
  size_t n = MIN(b->len, fb.size - (size_t)*pos);
  if (iob_write(b, 0, fb.virt + *pos, n)) return -EFAULT;
  *pos += n;
  return n;
}

static ssize_t fb_write(struct file *f, struct iobuf *b, loff_t *pos) {
  if ((u64)*pos >= fb.size) return b->len ? -ENOSPC : 0;
  size_t n = MIN(b->len, fb.size - (size_t)*pos);
  if (iob_read(b, 0, fb.virt + *pos, n)) return -EFAULT;
  *pos += n;
  return n;
}

static loff_t fb_llseek(struct file *f, loff_t off, int whence) {
  loff_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos : whence == SEEK_END ? (loff_t)fb.size : -1;
  if (base < 0 || base + off < 0) return -EINVAL;
  return f->pos = base + off;
}

static int fb_mmap(struct file *f, struct vma *v, u64 off) {
  if (off >= fb.size || v->end - v->start > ALIGN_UP(fb.size, PAGE_SIZE) - off) return -EINVAL;
  v->flags |= VM_IO | VM_NC;
  v->io_base = fb.phys + off;
  return 0;
}

static const struct file_operations fb_fops = {
    .read = fb_read, .write = fb_write, .llseek = fb_llseek, .ioctl = fb_ioctl, .mmap = fb_mmap};

static int rpi_fb_probe(int node) {
  if (!rpi_fw_available()) return -ENODEV;
  u32 wh[2] = {0, 0};
  if (rpi_fw_property(RPI_FW_FB_GET_PHYSICAL_WH, wh, 0, 8) < 0 || !wh[0] || !wh[1]) wh[0] = 1024, wh[1] = 768;
  /* one message: size, depth, pixel order, allocate, pitch */
  u32 m[36] = {0}, i = 2;
#define TAG(t, nwords, ...)                                                         \
  do {                                                                              \
    u32 vals[] = {__VA_ARGS__};                                                     \
    m[i++] = (t);                                                                   \
    m[i++] = (nwords) * 4;                                                          \
    m[i++] = 0;                                                                     \
    for (u32 k = 0; k < (nwords); k++) m[i++] = k < sizeof(vals) / 4 ? vals[k] : 0; \
  } while (0)
  TAG(RPI_FW_FB_SET_PHYSICAL_WH, 2, wh[0], wh[1]);
  TAG(RPI_FW_FB_SET_VIRTUAL_WH, 2, wh[0], wh[1] * 2); /* double buffering via pan */
  u32 depth_at = i;
  TAG(RPI_FW_FB_SET_DEPTH, 1, 32);
  TAG(RPI_FW_FB_SET_PIXEL_ORDER, 1, 1); /* RGB */
  u32 alloc_at = i;
  TAG(RPI_FW_FB_ALLOCATE, 2, 4096);
  u32 pitch_at = i;
  TAG(RPI_FW_FB_GET_PITCH, 1, 0);
  m[i++] = 0;
  m[0] = i * 4;
#undef TAG
  if (rpi_fw_message(m, i * 4) || !m[alloc_at + 3]) {
    pr_warn("fb: firmware refused to allocate a framebuffer\n");
    return -EIO;
  }
  fb.phys = m[alloc_at + 3] & 0x3fffffff; /* VideoCore bus address -> ARM physical */
  fb.size = m[alloc_at + 4];
  fb.pitch = m[pitch_at + 3];
  fb.width = wh[0];
  fb.height = wh[1];
  fb.depth = m[depth_at + 3] ? m[depth_at + 3] : 32;
  if (!fb.pitch) fb.pitch = fb.width * fb.depth / 8;
  fb.virt = ioremap_prot(fb.phys, fb.size, PROT_NORMAL_NC);
  if (!fb.virt) return -ENOMEM;
  register_chrdev(MKDEV(29, 0), "fb0", &fb_fops, NULL);
  devfs_create("fb0", S_IFCHR | 0660, MKDEV(29, 0));
  pr_info("fb0: %ux%u, %u bpp, pitch %u, %u KiB at %#llx\n", fb.width, fb.height, fb.depth, fb.pitch, fb.size >> 10,
          (unsigned long long)fb.phys);
  return 0;
}

DT_DRIVER(rpi_fb, DRV_DEVICE, rpi_fb_probe, "brcm,bcm2708-fb");
