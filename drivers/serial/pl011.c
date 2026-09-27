/*
 * ARM PrimeCell PL011 UART (QEMU virt, BCM2711 uart0..5).
 *
 * Provides an early console (fixmap, polled) before the MMU/vmalloc are
 * fully up, then an interrupt-driven TTY. Transmit is polled with the FIFO;
 * receive uses the RX/timeout interrupts.
 */
#include <asm/pgtable.h>
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/rpi_firmware.h>
#include <olux/tty.h>

#define UARTDR 0x00
#define UARTFR 0x18
#define UARTIBRD 0x24
#define UARTFBRD 0x28
#define UARTLCR_H 0x2c
#define UARTCR 0x30
#define UARTIFLS 0x34
#define UARTIMSC 0x38
#define UARTMIS 0x40
#define UARTICR 0x44
#define FR_TXFF (1 << 5)
#define FR_RXFE (1 << 4)
#define FR_BUSY (1 << 3)
#define LCRH_FEN (1 << 4)
#define LCRH_WLEN8 (3 << 5)
#define CR_UARTEN (1 << 0)
#define CR_TXE (1 << 8)
#define CR_RXE (1 << 9)
#define INT_RX (1 << 4)
#define INT_RT (1 << 6)
#define INT_OE (1 << 10)

struct pl011 {
  u8 *base;
  int node;
  u32 uartclk; /* reference clock, 0 until known */
  struct tty *tty;
  struct console con;
  spinlock_t lock;
  int irq;
};

static struct pl011 early;
static int nports;

static void pl011_putc(u8 *base, char c) {
  while (readl_relaxed(base + UARTFR) & FR_TXFF) __asm__ volatile("yield");
  writel_relaxed(c, base + UARTDR);
}

static void pl011_write_raw(struct pl011 *p, const char *s, size_t n, bool crlf) {
  unsigned long f = spin_lock_irqsave(&p->lock);
  for (size_t i = 0; i < n; i++) {
    if (crlf && s[i] == '\n') pl011_putc(p->base, '\r');
    pl011_putc(p->base, s[i]);
  }
  spin_unlock_irqrestore(&p->lock, f);
}

static void con_write(struct console *c, const char *s, size_t n) { pl011_write_raw(c->priv, s, n, true); }

static void tty_write(struct tty *t, const char *s, size_t n) { pl011_write_raw(t->priv, s, n, false); }

static void pl011_set_termios(struct tty *t, const struct termios *old);
static const struct tty_ops pl011_tty_ops = {.write = tty_write, .set_termios = pl011_set_termios};

static void pl011_irq(int irq, void *arg) {
  struct pl011 *p = arg;
  char buf[32];
  size_t n = 0;
  u32 mis = readl(p->base + UARTMIS);
  writel(mis & (INT_RX | INT_RT | INT_OE), p->base + UARTICR);
  while (!(readl_relaxed(p->base + UARTFR) & FR_RXFE)) {
    buf[n++] = readl_relaxed(p->base + UARTDR) & 0xff;
    if (n == sizeof(buf)) {
      tty_receive(p->tty, buf, n);
      n = 0;
    }
  }
  if (n) tty_receive(p->tty, buf, n);
}

/* Early console from /chosen/stdout-path, mapped through the fixmap. */
void pl011_early_init(void);
void pl011_early_init(void) {
  int chosen = fdt_path_offset("/chosen");
  const char *path = chosen >= 0 ? fdt_getprop_str(chosen, "stdout-path") : NULL;
  if (!path) return;
  char buf[128];
  strlcpy(buf, path, sizeof(buf));
  char *colon = strchr(buf, ':');
  if (colon) *colon = '\0';
  int node = fdt_path_offset(buf);
  if (node < 0 || !fdt_is_compatible(node, "arm,pl011")) return;
  u64 addr;
  if (fdt_get_reg(node, 0, &addr, NULL)) return;
  early.base = early_fixmap(addr, PROT_DEVICE);
  spin_lock_init(&early.lock);
  early.con.name = "earlycon";
  early.con.write = con_write;
  early.con.priv = &early;
  register_console(&early.con);
}

static bool is_stdout(int node) {
  int chosen = fdt_path_offset("/chosen");
  const char *path = chosen >= 0 ? fdt_getprop_str(chosen, "stdout-path") : NULL;
  if (!path) return nports == 0;
  char buf[128];
  strlcpy(buf, path, sizeof(buf));
  char *colon = strchr(buf, ':');
  if (colon) *colon = '\0';
  return fdt_path_offset(buf) == node;
}

/* Reference clock: firmware (Pi), DT fixed clock, or derived from the
 * divisor the firmware programmed for 115200 baud. */
static u32 pl011_clock(struct pl011 *p) {
  if (p->uartclk) return p->uartclk;
  u32 hz = 0;
  if (rpi_fw_available()) rpi_fw_clock_rate(RPI_CLK_UART, &hz);
  u32 ph;
  if (!hz && fdt_getprop_u32(p->node, "clocks", &ph)) {
    int cn = fdt_node_by_phandle(ph);
    if (cn >= 0) fdt_getprop_u32(cn, "clock-frequency", &hz);
  }
  if (!hz) {
    u32 ibrd = readl(p->base + UARTIBRD), fbrd = readl(p->base + UARTFBRD) & 63;
    if (ibrd) hz = (u32)((u64)(ibrd * 64 + fbrd) * 16 * 115200 / 64);
  }
  return p->uartclk = hz;
}

static void pl011_set_termios(struct tty *t, const struct termios *old) {
  struct pl011 *p = t->priv;
  u32 cflag = t->termios.c_cflag, baud = tty_baud(cflag), clk = pl011_clock(p);
  u32 lcrh = LCRH_FEN | ((cflag & CSIZE) >> 4) << 5;
  if (cflag & CSTOPB) lcrh |= 1 << 3;
  if (cflag & PARENB) lcrh |= (1 << 1) | ((cflag & PARODD) ? 0 : 1 << 2);
  unsigned long f = spin_lock_irqsave(&p->lock);
  while (readl(p->base + UARTFR) & FR_BUSY) __asm__ volatile("yield");
  u32 cr = readl(p->base + UARTCR);
  writel(0, p->base + UARTCR); /* the divisor latches on the LCR_H write, with the UART off */
  if (baud && clk && clk / 16 >= baud) {
    u32 div64 = (u32)(((u64)clk * 4 + baud / 2) / baud); /* clk / (16 * baud) in 1/64 units */
    writel(div64 >> 6, p->base + UARTIBRD);
    writel(div64 & 63, p->base + UARTFBRD);
  }
  writel(lcrh, p->base + UARTLCR_H);
  writel(cr, p->base + UARTCR);
  spin_unlock_irqrestore(&p->lock, f);
}

static int pl011_probe(int node) {
  struct pl011 *p = kzalloc(sizeof(*p), 0);
  if (!p) return -ENOMEM;
  p->base = dt_ioremap(node, 0, NULL);
  if (!p->base) return -ENOMEM;
  p->node = node;
  spin_lock_init(&p->lock);
  u32 irq, flags;
  if (fdt_get_irq(node, 0, &irq, &flags)) return -EINVAL;
  p->irq = irq;

  /* Keep the firmware-programmed baud rate; ensure 8N1 with FIFOs. */
  while (readl(p->base + UARTFR) & FR_BUSY);
  writel(readl(p->base + UARTLCR_H) | LCRH_FEN | LCRH_WLEN8, p->base + UARTLCR_H);
  writel(0, p->base + UARTIFLS); /* RX irq at 1/8 full */
  writel(0x7ff, p->base + UARTICR);
  writel(INT_RX | INT_RT | INT_OE, p->base + UARTIMSC);
  writel(CR_UARTEN | CR_TXE | CR_RXE, p->base + UARTCR);

  char name[16];
  snprintf(name, sizeof(name), "ttyAMA%d", nports++);
  p->tty = tty_register(name, &pl011_tty_ops, p);
  if (console_selected(name, is_stdout(node))) {
    p->con.name = "pl011";
    p->con.write = con_write;
    p->con.priv = p;
    p->con.no_replay = early.base != NULL;
    if (early.base) unregister_console(&early.con);
    early.base = NULL;
    register_console(&p->con);
    tty_set_console(p->tty);
  }
  int r = request_irq(p->irq, pl011_irq, p, "pl011");
  if (r) return r;
  pr_info("%s: PL011 at %s, IRQ %d%s\n", name, fdt_node_name(node), p->irq, p->con.write ? " (console)" : "");
  return 0;
}

DT_DRIVER(pl011, DRV_CONSOLE, pl011_probe, "arm,pl011");
