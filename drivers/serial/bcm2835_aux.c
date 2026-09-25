/*
 * BCM2835/BCM2711 "mini UART" (AUX UART1, ttyS0). The firmware enables and
 * clocks it when enable_uart=1; the driver keeps that configuration and
 * adds interrupt-driven receive.
 */
#include <asm/pgtable.h>
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/tty.h>

#define MU_IO 0x00
#define MU_IER 0x04
#define MU_IIR 0x08
#define MU_LSR 0x14
#define MU_CNTL 0x20
#define LSR_DATA_READY (1 << 0)
#define LSR_TX_EMPTY (1 << 5)

struct aux_uart {
  u8 *base;
  struct tty *tty;
  struct console con;
  spinlock_t lock;
};

static void mu_putc(u8 *base, char c) {
  while (!(readl_relaxed(base + MU_LSR) & LSR_TX_EMPTY)) __asm__ volatile("yield");
  writel_relaxed((u8)c, base + MU_IO);
}

static void mu_write(struct aux_uart *u, const char *s, size_t n, bool crlf) {
  unsigned long f = spin_lock_irqsave(&u->lock);
  for (size_t i = 0; i < n; i++) {
    if (crlf && s[i] == '\n') mu_putc(u->base, '\r');
    mu_putc(u->base, s[i]);
  }
  spin_unlock_irqrestore(&u->lock, f);
}

static void con_write(struct console *c, const char *s, size_t n) { mu_write(c->priv, s, n, true); }
static void tty_write(struct tty *t, const char *s, size_t n) { mu_write(t->priv, s, n, false); }
static const struct tty_ops aux_tty_ops = {.write = tty_write};

static void aux_irq(int irq, void *arg) {
  struct aux_uart *u = arg;
  char buf[16];
  size_t n = 0;
  while (readl_relaxed(u->base + MU_LSR) & LSR_DATA_READY) {
    buf[n++] = readl_relaxed(u->base + MU_IO) & 0xff;
    if (n == sizeof(buf)) {
      tty_receive(u->tty, buf, n);
      n = 0;
    }
  }
  if (n) tty_receive(u->tty, buf, n);
}

static bool is_stdout(int node) {
  int chosen = fdt_path_offset("/chosen");
  const char *path = chosen >= 0 ? fdt_getprop_str(chosen, "stdout-path") : NULL;
  if (!path) return false;
  char buf[128];
  strlcpy(buf, path, sizeof(buf));
  char *colon = strchr(buf, ':');
  if (colon) *colon = '\0';
  return fdt_path_offset(buf) == node;
}

static int aux_uart_probe(int node) {
  struct aux_uart *u = kzalloc(sizeof(*u), 0);
  if (!u) return -ENOMEM;
  u->base = dt_ioremap(node, 0, NULL);
  if (!u->base) return -ENOMEM;
  spin_lock_init(&u->lock);
  if (!(readl(u->base + MU_CNTL) & 3)) {
    /* not enabled by the firmware (enable_uart=0): leave it alone */
    return -ENODEV;
  }
  u->tty = tty_register("ttyS0", &aux_tty_ops, u);
  if (console_selected("ttyS0", is_stdout(node))) {
    u->con.name = "mini-uart";
    u->con.write = con_write;
    u->con.priv = u;
    register_console(&u->con);
    tty_set_console(u->tty);
  }
  u32 irq, flags;
  if (fdt_get_irq(node, 0, &irq, &flags) == 0) {
    writel(1, u->base + MU_IER); /* receive interrupt */
    request_irq(irq, aux_irq, u, "mini-uart");
  }
  pr_info("ttyS0: BCM2835 mini UART%s\n", u->con.write ? " (console)" : "");
  return 0;
}

DT_DRIVER(bcm2835_aux_uart, DRV_CONSOLE, aux_uart_probe, "brcm,bcm2835-aux-uart");
