/* In-kernel GPIO API (one SoC controller). */
#ifndef OLUX_GPIO_H
#define OLUX_GPIO_H

#include <olux/types.h>

int gpio_count(void);
int gpio_set_func(unsigned pin, unsigned func); /* GPIO_FUNC_* from uapi */
int gpio_get_func(unsigned pin);
int gpio_set_pull(unsigned pin, unsigned pull);
int gpio_get(unsigned pin);
int gpio_set(unsigned pin, int value);

#endif
