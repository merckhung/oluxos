/*
 * BCM2711 (and BCM2835) GPIO controller: function select, pulls, levels and
 * edge-detect interrupts, exported as /dev/gpiochip0.
 */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/fs.h>
#include <olux/gpio.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/sched.h>
#include <olux/spinlock.h>
#include <olux/time.h>
#include <olux/uaccess.h>
#include <olux/wait.h>
#include <uapi/olux/gpio.h>

#define GPFSEL(n) (0x00 + (n) * 4)
#define GPSET(n) (0x1c + (n) * 4)
#define GPCLR(n) (0x28 + (n) * 4)
#define GPLEV(n) (0x34 + (n) * 4)
#define GPEDS(n) (0x40 + (n) * 4)
#define GPREN(n) (0x4c + (n) * 4)
#define GPFEN(n) (0x58 + (n) * 4)
#define GPPUD 0x94 /* BCM2835 pull sequence */
#define GPPUDCLK(n) (0x98 + (n) * 4)
#define GPPUPPDN(n) (0xe4 + (n) * 4) /* BCM2711 pull control */

#define EVQ 64

struct gpio_file {
  u64 rising, falling; /* watched lines */
  struct gpio_event q[EVQ];
  unsigned head, tail;
  struct list_head link;
};

static struct {
  u8 *base;
  unsigned npins;
  bool is2711;
  spinlock_t lock;
  struct list_head files;
  struct wait_queue wq;
} G = {.lock = SPINLOCK_INIT};

int gpio_count(void) { return G.base ? (int)G.npins : 0; }

static bool bad(unsigned pin) { return !G.base || pin >= G.npins; }

int gpio_set_func(unsigned pin, unsigned func) {
  if (bad(pin) || func > 7) return -EINVAL;
  unsigned long f = spin_lock_irqsave(&G.lock);
  u32 v = readl(G.base + GPFSEL(pin / 10));
  v &= ~(7u << (pin % 10 * 3));
  v |= func << (pin % 10 * 3);
  writel(v, G.base + GPFSEL(pin / 10));
  spin_unlock_irqrestore(&G.lock, f);
  return 0;
}

int gpio_get_func(unsigned pin) {
  if (bad(pin)) return -EINVAL;
  return (readl(G.base + GPFSEL(pin / 10)) >> (pin % 10 * 3)) & 7;
}

int gpio_set_pull(unsigned pin, unsigned pull) {
  if (bad(pin) || pull > 2) return -EINVAL;
  unsigned long f = spin_lock_irqsave(&G.lock);
  if (G.is2711) {
    u32 v = readl(G.base + GPPUPPDN(pin / 16));
    v &= ~(3u << (pin % 16 * 2));
    v |= pull << (pin % 16 * 2);
    writel(v, G.base + GPPUPPDN(pin / 16));
  } else { /* BCM2835: control, clock the line, release */
    writel(pull == GPIO_PULL_UP ? 2 : pull == GPIO_PULL_DOWN ? 1 : 0, G.base + GPPUD);
    udelay(1);
    writel(1u << (pin % 32), G.base + GPPUDCLK(pin / 32));
    udelay(1);
    writel(0, G.base + GPPUD);
    writel(0, G.base + GPPUDCLK(pin / 32));
  }
  spin_unlock_irqrestore(&G.lock, f);
  return 0;
}

int gpio_get(unsigned pin) {
  if (bad(pin)) return -EINVAL;
  return (readl(G.base + GPLEV(pin / 32)) >> (pin % 32)) & 1;
}

int gpio_set(unsigned pin, int value) {
  if (bad(pin)) return -EINVAL;
  writel(1u << (pin % 32), G.base + (value ? GPSET(pin / 32) : GPCLR(pin / 32)));
  return 0;
}

/* Apply a node's "default" pin configuration (pinctrl-0 -> brcm,pins /
 * brcm,function / brcm,pull), as Linux's pinctrl driver would. */
int gpio_apply_pinctrl(int node) {
  if (!G.base) return -ENODEV;
  int len;
  const u32 *ph = fdt_getprop(node, "pinctrl-0", &len);
  for (int i = 0; ph && i < len / 4; i++) {
    int cfg = fdt_node_by_phandle(fdt32(ph[i]));
    if (cfg < 0) continue;
    int np, nf, nu;
    const u32 *pins = fdt_getprop(cfg, "brcm,pins", &np);
    const u32 *fn = fdt_getprop(cfg, "brcm,function", &nf);
    const u32 *pull = fdt_getprop(cfg, "brcm,pull", &nu);
    for (int k = 0; pins && k < np / 4; k++) {
      unsigned pin = fdt32(pins[k]);
      if (fn && nf >= 4) gpio_set_func(pin, fdt32(fn[nf / 4 > k ? k : 0]));
      if (pull && nu >= 4)
        gpio_set_pull(pin, fdt32(pull[nu / 4 > k ? k : 0]) == 2   ? GPIO_PULL_UP
                           : fdt32(pull[nu / 4 > k ? k : 0]) == 1 ? GPIO_PULL_DOWN
                                                                  : GPIO_PULL_NONE);
    }
  }
  return 0;
}

/* Program edge detection for the union of all watchers. Lock held. */
static void update_detect(void) {
  u64 r = 0, fl = 0;
  struct gpio_file *gf;
  list_for_each_entry(gf, &G.files, link) {
    r |= gf->rising;
    fl |= gf->falling;
  }
  for (int b = 0; b < 2; b++) {
    writel((u32)(r >> (32 * b)), G.base + GPREN(b));
    writel((u32)(fl >> (32 * b)), G.base + GPFEN(b));
  }
}

static void gpio_irq(int irq, void *arg) {
  u64 now = ktime_ns();
  spin_lock(&G.lock);
  for (int b = 0; b < 2; b++) {
    u32 ev = readl(G.base + GPEDS(b));
    if (!ev) continue;
    writel(ev, G.base + GPEDS(b)); /* write-1-to-clear */
    u32 lev = readl(G.base + GPLEV(b));
    while (ev) {
      int bit = __builtin_ctz(ev);
      ev &= ev - 1;
      unsigned pin = b * 32 + bit;
      u32 edge = (lev >> bit) & 1 ? GPIO_EDGE_RISING : GPIO_EDGE_FALLING;
      struct gpio_file *gf;
      list_for_each_entry(gf, &G.files, link) {
        u64 m = 1ULL << pin;
        if (!((edge == GPIO_EDGE_RISING ? gf->rising : gf->falling) & m)) continue;
        if (gf->head - gf->tail >= EVQ) continue; /* queue full: drop */
        gf->q[gf->head++ % EVQ] = (struct gpio_event){pin, edge, now};
      }
    }
  }
  spin_unlock(&G.lock);
  wake_up(&G.wq);
}

/* ---------------- /dev/gpiochip0 ---------------- */

static int gpiochip_open(struct inode *i, struct file *f) {
  struct gpio_file *gf = kzalloc(sizeof(*gf), 0);
  if (!gf) return -ENOMEM;
  unsigned long fl = spin_lock_irqsave(&G.lock);
  list_add(&gf->link, &G.files);
  spin_unlock_irqrestore(&G.lock, fl);
  f->priv = gf;
  return 0;
}

static int gpiochip_release(struct inode *i, struct file *f) {
  struct gpio_file *gf = f->priv;
  unsigned long fl = spin_lock_irqsave(&G.lock);
  list_del(&gf->link);
  update_detect();
  spin_unlock_irqrestore(&G.lock, fl);
  kfree(gf);
  return 0;
}

static long gpiochip_ioctl(struct file *f, unsigned cmd, u64 arg) {
  struct gpio_file *gf = f->priv;
  struct gpio_line l;
  if (copy_from_user(&l, arg, sizeof(l))) return -EFAULT;
  int r = 0;
  switch (cmd) {
    case GPIO_INFO:
      l.value = G.npins;
      break;
    case GPIO_GET_FUNC:
      r = gpio_get_func(l.pin);
      if (r >= 0) l.value = r, r = 0;
      break;
    case GPIO_SET_FUNC:
      r = gpio_set_func(l.pin, l.value);
      break;
    case GPIO_GET:
      r = gpio_get(l.pin);
      if (r >= 0) l.value = r, r = 0;
      break;
    case GPIO_SET:
      r = gpio_set(l.pin, l.value != 0);
      break;
    case GPIO_SET_PULL:
      r = gpio_set_pull(l.pin, l.value);
      break;
    case GPIO_WATCH: {
      if (bad(l.pin) || l.value > 3) return -EINVAL;
      u64 m = 1ULL << l.pin;
      unsigned long fl = spin_lock_irqsave(&G.lock);
      gf->rising = (l.value & GPIO_EDGE_RISING) ? gf->rising | m : gf->rising & ~m;
      gf->falling = (l.value & GPIO_EDGE_FALLING) ? gf->falling | m : gf->falling & ~m;
      update_detect();
      spin_unlock_irqrestore(&G.lock, fl);
      break;
    }
    default:
      return -ENOTTY;
  }
  if (!r && copy_to_user(arg, &l, sizeof(l))) r = -EFAULT;
  return r;
}

static ssize_t gpiochip_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct gpio_file *gf = f->priv;
  if (b->len < sizeof(struct gpio_event)) return -EINVAL;
  if (gf->head == gf->tail) {
    if (f->flags & O_NONBLOCK) return -EAGAIN;
    int r = wait_event_interruptible(G.wq, gf->head != gf->tail);
    if (r) return r;
  }
  size_t done = 0;
  while (done + sizeof(struct gpio_event) <= b->len) {
    unsigned long fl = spin_lock_irqsave(&G.lock);
    if (gf->head == gf->tail) {
      spin_unlock_irqrestore(&G.lock, fl);
      break;
    }
    struct gpio_event e = gf->q[gf->tail++ % EVQ];
    spin_unlock_irqrestore(&G.lock, fl);
    if (iob_write(b, done, &e, sizeof(e))) return done ? (ssize_t)done : -EFAULT;
    done += sizeof(e);
  }
  return done;
}

static unsigned gpiochip_poll(struct file *f, struct poll_table *pt) {
  struct gpio_file *gf = f->priv;
  poll_wait(f, &G.wq, pt);
  return gf->head != gf->tail ? POLLIN | POLLRDNORM : 0;
}

static const struct file_operations gpiochip_fops = {
    .open = gpiochip_open,
    .release = gpiochip_release,
    .read = gpiochip_read,
    .poll = gpiochip_poll,
    .ioctl = gpiochip_ioctl,
};

static int gpio_probe(int node) {
  G.base = dt_ioremap(node, 0, NULL);
  if (!G.base) return -ENOMEM;
  G.is2711 = fdt_is_compatible(node, "brcm,bcm2711-gpio");
  G.npins = G.is2711 ? 58 : 54;
  list_init(&G.files);
  wq_init(&G.wq);
  for (int b = 0; b < 2; b++) { /* start with detection off and events clear */
    writel(0, G.base + GPREN(b));
    writel(0, G.base + GPFEN(b));
    writel(~0u, G.base + GPEDS(b));
  }
  for (int i = 0; i < 2; i++) { /* bank 0 and bank 1 interrupts */
    u32 irq, flags;
    if (!fdt_get_irq(node, i, &irq, &flags)) request_irq(irq, gpio_irq, NULL, "gpio");
  }
  register_chrdev(MKDEV(254, 0), "gpiochip0", &gpiochip_fops, NULL);
  devfs_create("gpiochip0", S_IFCHR | 0600, MKDEV(254, 0));
  pr_info("gpio: %s controller, %u lines\n", G.is2711 ? "BCM2711" : "BCM2835", G.npins);
  return 0;
}

DT_DRIVER(bcm_gpio, DRV_DEVICE, gpio_probe, "brcm,bcm2711-gpio", "brcm,bcm2835-gpio");
