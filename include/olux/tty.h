#ifndef OLUX_TTY_H
#define OLUX_TTY_H

#include <olux/types.h>
#include <olux/wait.h>

/* Linux-compatible termios (struct termios2 layout used by musl ioctls). */
#define NCCS 19
struct termios {
  u32 c_iflag, c_oflag, c_cflag, c_lflag;
  u8 c_line;
  u8 c_cc[NCCS];
};

struct winsize {
  u16 ws_row, ws_col, ws_xpixel, ws_ypixel;
};

/* c_cc indices */
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11
#define VWERASE 14
#define VLNEXT 15
/* c_iflag */
#define IGNBRK 0000001
#define BRKINT 0000002
#define IGNCR 0000200
#define ICRNL 0000400
#define INLCR 0000100
#define IXON 0002000
#define IUTF8 0040000
/* c_oflag */
#define OPOST 0000001
#define ONLCR 0000004
/* c_cflag */
#define CBAUD 0010017
#define CBAUDEX 0010000
#define CSIZE 0000060
#define CS5 0000000
#define CS6 0000020
#define CS7 0000040
#define CS8 0000060
#define CSTOPB 0000100
#define CREAD 0000200
#define PARENB 0000400
#define PARODD 0001000
#define B115200 0010002

/* Baud rate encoded in c_cflag, or 0 if unknown. */
static inline u32 tty_baud(u32 cflag) {
  static const u32 low[16] = {0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200, 38400};
  static const u32 high[16] = {0,       57600,   115200,  230400,  460800,  500000,  576000,  921600,
                               1000000, 1152000, 1500000, 2000000, 2500000, 3000000, 3500000, 4000000};
  return (cflag & CBAUDEX) ? high[cflag & 017] : low[cflag & 017];
}
/* c_lflag */
#define ISIG 0000001
#define ICANON 0000002
#define ECHO 0000010
#define ECHOE 0000020
#define ECHOK 0000040
#define ECHONL 0000100
#define NOFLSH 0000200
#define TOSTOP 0000400
#define ECHOCTL 0001000
#define ECHOKE 0004000
#define IEXTEN 0100000

struct tty;
struct tty_ops {
  void (*write)(struct tty *t, const char *buf, size_t n);
  void (*set_termios)(struct tty *t, const struct termios *old);
  /* Optional: bytes the output side can accept now (flow control). */
  unsigned (*write_room)(struct tty *t);
};

#define TTY_BUF 4096
struct process;

struct tty {
  char name[16];
  int index;
  const struct tty_ops *ops;
  void *priv;
  struct termios termios;
  struct winsize winsize;
  spinlock_t lock;
  /* input: raw ring + completed-line boundary for canonical mode */
  char ibuf[TTY_BUF];
  unsigned ihead, itail, icanon_end;
  unsigned column;
  bool lnext;
  struct wait_queue read_wait, write_wait;
  /* job control */
  int pgrp;
  int session;
  int opens;
  bool hangup;
};

struct tty *tty_register(const char *name, const struct tty_ops *ops, void *priv);
struct file;
struct file_operations;
/* Dynamic ttys (pseudo-terminals): not listed as /dev/ttyXXX. */
struct tty *tty_alloc(const char *name, const struct tty_ops *ops, void *priv);
void tty_free(struct tty *t); /* detaches it from every session */
unsigned tty_input_room(struct tty *t);
void tty_hangup(struct tty *t);
int tty_attach(struct tty *t, struct file *f);
const struct file_operations *tty_generic_fops(void);
void tty_receive(struct tty *t, const char *buf, size_t n); /* IRQ context */
struct tty *tty_console(void);
void tty_set_console(struct tty *t);
void tty_init(void);

#endif
