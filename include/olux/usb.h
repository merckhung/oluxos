/* USB core: devices, interfaces, class drivers and host controller ops. */
#ifndef OLUX_USB_H
#define OLUX_USB_H

#include <olux/compiler.h>
#include <olux/list.h>
#include <olux/types.h>

/* standard requests */
#define USB_DIR_IN 0x80
#define USB_TYPE_CLASS 0x20
#define USB_RECIP_INTERFACE 0x01
#define USB_RECIP_ENDPOINT 0x02
#define USB_REQ_CLEAR_FEATURE 0x01
#define USB_REQ_SET_FEATURE 0x03
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_SET_INTERFACE 0x0b
#define USB_DT_DEVICE 1
#define USB_DT_CONFIG 2
#define USB_DT_STRING 3
#define USB_DT_INTERFACE 4
#define USB_DT_ENDPOINT 5
#define USB_DT_SS_EP_COMP 0x30
#define USB_ENDPOINT_HALT 0

#define USB_EP_XFER_CONTROL 0
#define USB_EP_XFER_ISOC 1
#define USB_EP_XFER_BULK 2
#define USB_EP_XFER_INT 3

/* xHCI speed IDs */
#define USB_SPEED_FULL 1
#define USB_SPEED_LOW 2
#define USB_SPEED_HIGH 3
#define USB_SPEED_SUPER 4

#define USB_MAX_INTERFACES 8
#define USB_MAX_EP 16

struct usb_device;
struct usb_interface;

struct usb_endpoint {
  u8 addr; /* bit 7 = IN */
  u8 type; /* USB_EP_XFER_* */
  u16 mps;
  u8 interval;
  u8 max_burst; /* SuperSpeed companion */
};

struct usb_interface {
  struct usb_device *dev;
  u8 number, alt, cls, subcls, proto;
  int nep;
  struct usb_endpoint ep[USB_MAX_EP];
  const struct usb_driver *driver;
  void *priv;
};

typedef void (*usb_complete_t)(struct usb_interface *intf, const u8 *data, int len, int status);

struct usb_hc_ops {
  /* Synchronous transfers; return the byte count or -errno (-EPIPE: stall). */
  int (*control)(struct usb_device *d, u8 reqtype, u8 req, u16 value, u16 index, void *data, u16 len, int timeout_ms);
  int (*bulk)(struct usb_device *d, u8 ep, void *data, u32 len, int timeout_ms);
  /* Poll an interrupt IN endpoint; `done` runs in interrupt context and the
   * transfer is resubmitted after it returns. */
  int (*intr_start)(struct usb_interface *intf, u8 ep, u16 len, usb_complete_t done);
  /* Set up the endpoints of every interface after SET_CONFIGURATION. */
  int (*configure)(struct usb_device *d);
  /* Clear a halted endpoint (host side and device side). */
  int (*clear_halt)(struct usb_device *d, u8 ep);
};

struct usb_device {
  const struct usb_hc_ops *ops;
  void *hcpriv;
  int port, speed;
  u16 vendor, product;
  u8 cls, subcls, proto, mps0;
  char manufacturer[48], product_name[48], serial[48];
  u8 config_value;
  int nintf;
  struct usb_interface intf[USB_MAX_INTERFACES];
  volatile bool gone; /* unplugged: transfers fail */
  int devnum;
};

struct usb_driver {
  const char *name;
  u8 cls, subcls, proto; /* 0xff = any */
  int (*probe)(struct usb_interface *intf);
  void (*disconnect)(struct usb_interface *intf);
};

#define USB_ANY 0xff
#define USB_DRIVER(ident, ...) \
  static const struct usb_driver ident##_usbdrv __used __section(".usb_drivers") = {__VA_ARGS__}

/* Called by host controller drivers once endpoint 0 works. */
int usb_new_device(struct usb_device *d);
void usb_disconnect(struct usb_device *d);

int usb_control(struct usb_device *d, u8 reqtype, u8 req, u16 value, u16 index, void *data, u16 len);
const char *usb_speed_name(int speed);

#endif
