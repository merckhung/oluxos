/* Input devices: /dev/input/eventN with the Linux evdev ABI. */
#ifndef OLUX_INPUT_H
#define OLUX_INPUT_H

#include <olux/types.h>

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_MSC 0x04
#define EV_REP 0x14
#define SYN_REPORT 0
#define REL_X 0x00
#define REL_Y 0x01
#define REL_WHEEL 0x08
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define KEY_MAX 0x2ff
#define BUS_USB 0x03

struct input_dev;

struct input_dev *input_register(const char *name, u16 bustype, u16 vendor, u16 product, u32 evbits);
void input_set_key(struct input_dev *d, unsigned code); /* advertise a key/button */
void input_set_rel(struct input_dev *d, unsigned code);
void input_event(struct input_dev *d, unsigned type, unsigned code, int value); /* any context */
static inline void input_sync(struct input_dev *d) { input_event(d, EV_SYN, SYN_REPORT, 0); }
void input_unregister(struct input_dev *d);

#endif
