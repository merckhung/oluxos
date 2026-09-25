/* gpio - inspect and drive GPIO lines through /dev/gpiochip0.
 *
 *   gpio info                       list lines with function and level
 *   gpio get <pin>
 *   gpio set <pin> <0|1>            (switches the line to output)
 *   gpio func <pin> in|out|alt0..alt5
 *   gpio pull <pin> up|down|none
 *   gpio wait <pin> rising|falling|both [timeout_ms]
 */
#include <errno.h>
#include <fcntl.h>
#include <olux/gpio.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char *const func_names[8] = {"in", "out", "alt5", "alt4", "alt0", "alt1", "alt2", "alt3"};

static int fd;

static int xioctl(unsigned cmd, unsigned pin, unsigned value, unsigned *out) {
  struct gpio_line l = {pin, value};
  if (ioctl(fd, cmd, &l) < 0) {
    fprintf(stderr, "gpio: line %u: %s\n", pin, strerror(errno));
    exit(1);
  }
  if (out) *out = l.value;
  return 0;
}

static unsigned pin_arg(const char *s) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end || v > 63) {
    fprintf(stderr, "gpio: bad line number '%s'\n", s);
    exit(2);
  }
  return (unsigned)v;
}

static int usage(void) {
  fprintf(stderr,
          "usage: gpio info | get PIN | set PIN 0|1 | func PIN in|out|alt0..alt5 |\n"
          "            pull PIN up|down|none | wait PIN rising|falling|both [TIMEOUT_MS]\n");
  return 2;
}

int main(int argc, char **argv) {
  if (argc < 2) return usage();
  fd = open("/dev/gpiochip0", O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    perror("gpio: /dev/gpiochip0");
    return 1;
  }
  const char *cmd = argv[1];
  unsigned v;
  if (!strcmp(cmd, "info")) {
    unsigned n;
    xioctl(GPIO_INFO, 0, 0, &n);
    for (unsigned p = 0; p < n; p++) {
      unsigned func, lev;
      xioctl(GPIO_GET_FUNC, p, 0, &func);
      xioctl(GPIO_GET, p, 0, &lev);
      printf("GPIO%-2u %-4s %u\n", p, func_names[func & 7], lev);
    }
    return 0;
  }
  if (argc < 3) return usage();
  unsigned pin = pin_arg(argv[2]);
  if (!strcmp(cmd, "get")) {
    xioctl(GPIO_GET, pin, 0, &v);
    printf("%u\n", v);
  } else if (!strcmp(cmd, "set") && argc == 4) {
    xioctl(GPIO_SET_FUNC, pin, GPIO_FUNC_OUT, NULL);
    xioctl(GPIO_SET, pin, atoi(argv[3]) != 0, NULL);
  } else if (!strcmp(cmd, "func") && argc == 4) {
    unsigned f = 8;
    for (unsigned i = 0; i < 8; i++)
      if (!strcmp(argv[3], func_names[i])) f = i;
    if (f == 8) return usage();
    xioctl(GPIO_SET_FUNC, pin, f, NULL);
  } else if (!strcmp(cmd, "pull") && argc == 4) {
    unsigned p = !strcmp(argv[3], "up")     ? GPIO_PULL_UP
                 : !strcmp(argv[3], "down") ? GPIO_PULL_DOWN
                 : !strcmp(argv[3], "none") ? GPIO_PULL_NONE
                                            : 9;
    if (p == 9) return usage();
    xioctl(GPIO_SET_PULL, pin, p, NULL);
  } else if (!strcmp(cmd, "wait") && argc >= 4) {
    unsigned e = !strcmp(argv[3], "rising")    ? GPIO_EDGE_RISING
                 : !strcmp(argv[3], "falling") ? GPIO_EDGE_FALLING
                 : !strcmp(argv[3], "both")    ? 3
                                               : 0;
    if (!e) return usage();
    int timeout = argc > 4 ? atoi(argv[4]) : -1;
    xioctl(GPIO_WATCH, pin, e, NULL);
    struct pollfd pfd = {fd, POLLIN, 0};
    int r = poll(&pfd, 1, timeout);
    if (r <= 0) {
      fprintf(stderr, "gpio: %s\n", r == 0 ? "timed out" : strerror(errno));
      return 1;
    }
    struct gpio_event ev;
    if (read(fd, &ev, sizeof(ev)) == sizeof(ev))
      printf("GPIO%u %s at %llu.%09llu\n", ev.pin, ev.edge == GPIO_EDGE_RISING ? "rising" : "falling",
             (unsigned long long)(ev.timestamp_ns / 1000000000), (unsigned long long)(ev.timestamp_ns % 1000000000));
  } else {
    return usage();
  }
  return 0;
}
