/*
 * USB HID boot-protocol keyboards and mice. Reports become evdev events
 * (/dev/input/eventN); keyboard input is also typed into the console tty
 * (US layout, with key repeat), so a USB keyboard works as a terminal.
 */
#include <olux/input.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/time.h>
#include <olux/tty.h>
#include <olux/usb.h>

#define HID_REQ_SET_IDLE 0x0a
#define HID_REQ_SET_PROTOCOL 0x0b
#define REPEAT_DELAY_NS (500 * 1000000ULL)
#define REPEAT_RATE_NS (33 * 1000000ULL)

/* HID usage (keyboard page) -> Linux key code, as in Linux's hid-input */
static const u8 hid_keyboard[0x74] = {
    0,   0,   0,   0,   30,  48,  46,  32,  18,  33,  34,  35,  23,  36,  37,  38,  50,  49,  24,  25, 16, 19, 31, 20,
    22,  47,  17,  45,  21,  44,  2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  28,  1,   14,  15, 57, 12, 13, 26,
    27,  43,  43,  39,  40,  41,  51,  52,  53,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68, 87, 88, 99, 70,
    119, 110, 102, 104, 111, 107, 109, 106, 105, 108, 103, 69,  98,  55,  74,  78,  96,  79,  80,  81, 75, 76, 77, 71,
    72,  73,  82,  83,  86,  127, 116, 117, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194};
/* modifier bits 0..7 -> LeftCtrl, LeftShift, LeftAlt, LeftMeta, RightCtrl, RightShift, RightAlt, RightMeta */
static const u8 hid_modifiers[8] = {29, 42, 56, 125, 97, 54, 100, 126};

/* Linux key code -> character (US layout), unshifted / shifted */
static const char keymap[2][59] = {
    {0,    27,  '1', '2',  '3', '4', '5', '6', '7',  '8', '9', '0', '-', '=', 127, '\t', 'q', 'w', 'e', 'r',
     't',  'y', 'u', 'i',  'o', 'p', '[', ']', '\r', 0,   'a', 's', 'd', 'f', 'g', 'h',  'j', 'k', 'l', ';',
     '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v', 'b',  'n', 'm', ',', '.', '/', 0,   '*',  0,   ' ', 0},
    {0,   27,  '!', '@', '#', '$', '%', '^', '&',  '*', '(', ')', '_', '+', 127, '\t', 'Q', 'W', 'E', 'R',
     'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\r', 0,   'A', 'S', 'D', 'F', 'G', 'H',  'J', 'K', 'L', ':',
     '"', '~', 0,   '|', 'Z', 'X', 'C', 'V', 'B',  'N', 'M', '<', '>', '?', 0,   '*',  0,   ' ', 0},
};

struct hid {
  struct usb_interface *intf;
  struct input_dev *input;
  bool keyboard;
  u8 prev[8];
  bool caps;
  /* console key repeat */
  u8 repeat_key;
  struct ktimer repeat;
};

/* Characters for a key on the console, or NULL. */
static const char *key_chars(struct hid *h, u8 key, u8 mods, char *buf) {
  bool shift = mods & 0x22, ctrl = mods & 0x11;
  switch (key) { /* cursor and editing keys: VT100 sequences */
    case 103:
      return "\033[A";
    case 108:
      return "\033[B";
    case 106:
      return "\033[C";
    case 105:
      return "\033[D";
    case 102:
      return "\033[H";
    case 107:
      return "\033[F";
    case 111:
      return "\033[3~";
    case 104:
      return "\033[5~";
    case 109:
      return "\033[6~";
    case 96:
      return "\r";
  }
  if (key >= sizeof(keymap[0]) || !keymap[0][key]) return NULL;
  char c = keymap[shift ? 1 : 0][key];
  if (h->caps && c >= 'a' && c <= 'z')
    c -= 32;
  else if (h->caps && c >= 'A' && c <= 'Z' && shift)
    c += 32;
  if (ctrl) {
    if (c >= 'a' && c <= 'z')
      c = (char)(c - 'a' + 1);
    else if (c >= 'A' && c <= 'Z')
      c = (char)(c - 'A' + 1);
    else if (c == '[' || c == '{')
      c = 27;
    else if (c == '\\' || c == '|')
      c = 28;
    else if (c == ']' || c == '}')
      c = 29;
    else if (c == ' ')
      c = 0;
  }
  buf[0] = c;
  buf[1] = 0;
  return c || ctrl ? buf : NULL;
}

static void type_key(struct hid *h, u8 key) {
  char buf[2];
  const char *s = key_chars(h, key, h->prev[0], buf);
  struct tty *con = tty_console();
  if (s && con) tty_receive(con, s, s[0] ? strlen(s) : 1);
}

static void repeat_fn(struct ktimer *t) {
  struct hid *h = t->arg;
  if (!h->repeat_key || h->intf->dev->gone) return;
  type_key(h, h->repeat_key);
  ktimer_start(&h->repeat, ktime_ns() + REPEAT_RATE_NS);
}

static bool in_report(const u8 *r, u8 usage) {
  for (int i = 2; i < 8; i++)
    if (r[i] == usage) return true;
  return false;
}

static void keyboard_report(struct hid *h, const u8 *r, int len) {
  if (len < 8 || r[2] == 1) return; /* rollover error */
  u8 mods = r[0], pmods = h->prev[0];
  for (int b = 0; b < 8; b++)
    if ((mods ^ pmods) & (1u << b)) input_event(h->input, EV_KEY, hid_modifiers[b], (mods >> b) & 1);
  for (int i = 2; i < 8; i++) { /* releases */
    u8 u = h->prev[i];
    if (u >= 4 && u < sizeof(hid_keyboard) && !in_report(r, u)) {
      input_event(h->input, EV_KEY, hid_keyboard[u], 0);
      if (hid_keyboard[u] == h->repeat_key) h->repeat_key = 0;
    }
  }
  h->prev[0] = mods;
  for (int i = 2; i < 8; i++) { /* presses */
    u8 u = r[i];
    if (u >= 4 && u < sizeof(hid_keyboard) && !in_report(h->prev, u)) {
      u8 key = hid_keyboard[u];
      input_event(h->input, EV_KEY, key, 1);
      if (key == 58) h->caps = !h->caps;
      type_key(h, key);
      h->repeat_key = key;
      ktimer_start(&h->repeat, ktime_ns() + REPEAT_DELAY_NS);
    }
  }
  memcpy(h->prev, r, 8);
  input_sync(h->input);
}

static void mouse_report(struct hid *h, const u8 *r, int len) {
  if (len < 3) return;
  u8 b = r[0], pb = h->prev[0];
  static const u16 btn[3] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE};
  for (int i = 0; i < 3; i++)
    if ((b ^ pb) & (1u << i)) input_event(h->input, EV_KEY, btn[i], (b >> i) & 1);
  h->prev[0] = b;
  if (r[1]) input_event(h->input, EV_REL, REL_X, (s8)r[1]);
  if (r[2]) input_event(h->input, EV_REL, REL_Y, (s8)r[2]);
  if (len > 3 && r[3]) input_event(h->input, EV_REL, REL_WHEEL, (s8)r[3]);
  input_sync(h->input);
}

static void hid_complete(struct usb_interface *intf, const u8 *data, int len, int status) {
  struct hid *h = intf->priv;
  if (status || !h) return;
  if (h->keyboard)
    keyboard_report(h, data, len);
  else
    mouse_report(h, data, len);
}

static int hid_probe(struct usb_interface *intf) {
  if (intf->proto != 1 && intf->proto != 2) return -ENODEV; /* boot keyboard / mouse only */
  const struct usb_endpoint *ep = NULL;
  for (int i = 0; i < intf->nep; i++)
    if (intf->ep[i].type == USB_EP_XFER_INT && (intf->ep[i].addr & USB_DIR_IN)) ep = &intf->ep[i];
  if (!ep) return -ENODEV;
  struct usb_device *d = intf->dev;
  struct hid *h = kzalloc(sizeof(*h), 0);
  if (!h) return -ENOMEM;
  h->intf = intf;
  h->keyboard = intf->proto == 1;
  /* boot protocol, no idle reports */
  usb_control(d, USB_TYPE_CLASS | USB_RECIP_INTERFACE, HID_REQ_SET_PROTOCOL, 0, intf->number, NULL, 0);
  usb_control(d, USB_TYPE_CLASS | USB_RECIP_INTERFACE, HID_REQ_SET_IDLE, 0, intf->number, NULL, 0);
  char name[64];
  snprintf(name, sizeof(name), "%s", d->product_name[0] ? d->product_name : h->keyboard ? "USB keyboard" : "USB mouse");
  h->input = input_register(name, BUS_USB, d->vendor, d->product,
                            h->keyboard ? (1u << EV_KEY | 1u << EV_REP) : (1u << EV_KEY | 1u << EV_REL));
  if (!h->input) {
    kfree(h);
    return -ENOMEM;
  }
  if (h->keyboard) {
    for (unsigned u = 4; u < sizeof(hid_keyboard); u++)
      if (hid_keyboard[u]) input_set_key(h->input, hid_keyboard[u]);
    for (int b = 0; b < 8; b++) input_set_key(h->input, hid_modifiers[b]);
  } else {
    input_set_key(h->input, BTN_LEFT);
    input_set_key(h->input, BTN_RIGHT);
    input_set_key(h->input, BTN_MIDDLE);
    input_set_rel(h->input, REL_X);
    input_set_rel(h->input, REL_Y);
    input_set_rel(h->input, REL_WHEEL);
  }
  ktimer_init(&h->repeat, repeat_fn, h);
  intf->priv = h;
  int r = d->ops->intr_start(intf, ep->addr, (u16)MIN(ep->mps, (u16)64), hid_complete);
  if (r) {
    input_unregister(h->input);
    intf->priv = NULL;
    kfree(h);
    return r;
  }
  return 0;
}

static void hid_disconnect(struct usb_interface *intf) {
  struct hid *h = intf->priv;
  if (!h) return;
  h->repeat_key = 0;
  ktimer_cancel(&h->repeat);
  input_unregister(h->input);
}

USB_DRIVER(hid, .name = "usbhid", .cls = 3, .subcls = 1, .proto = USB_ANY, .probe = hid_probe,
           .disconnect = hid_disconnect);
