/*
 * /dev/gpiochip0: GPIO lines of the SoC.
 *
 * ioctl(fd, cmd, struct gpio_line *): `pin` selects the line, `value` is
 * the argument or the result. read() returns struct gpio_event records for
 * the edges this file watches (GPIO_WATCH); poll() reports POLLIN when an
 * event is queued.
 */
#ifndef UAPI_OLUX_GPIO_H
#define UAPI_OLUX_GPIO_H

#include <stdint.h>

struct gpio_line {
  uint32_t pin;
  uint32_t value;
};

struct gpio_event {
  uint32_t pin;
  uint32_t edge;         /* GPIO_EDGE_RISING or GPIO_EDGE_FALLING */
  uint64_t timestamp_ns; /* CLOCK_MONOTONIC */
};

/* _IOWR('G', n, struct gpio_line) */
#define GPIO_IOC(n) (0xC0084700u | (n))
#define GPIO_INFO GPIO_IOC(0)     /* value <- number of lines */
#define GPIO_GET_FUNC GPIO_IOC(1) /* value <- GPIO_FUNC_* */
#define GPIO_SET_FUNC GPIO_IOC(2)
#define GPIO_GET GPIO_IOC(3)      /* value <- level (0/1) */
#define GPIO_SET GPIO_IOC(4)      /* drive an output */
#define GPIO_SET_PULL GPIO_IOC(5) /* GPIO_PULL_* */
#define GPIO_WATCH GPIO_IOC(6)    /* value = GPIO_EDGE_* mask, 0 stops */

#define GPIO_FUNC_IN 0
#define GPIO_FUNC_OUT 1
#define GPIO_FUNC_ALT0 4
#define GPIO_FUNC_ALT1 5
#define GPIO_FUNC_ALT2 6
#define GPIO_FUNC_ALT3 7
#define GPIO_FUNC_ALT4 3
#define GPIO_FUNC_ALT5 2

#define GPIO_PULL_NONE 0
#define GPIO_PULL_UP 1
#define GPIO_PULL_DOWN 2

#define GPIO_EDGE_RISING 1
#define GPIO_EDGE_FALLING 2

#endif
