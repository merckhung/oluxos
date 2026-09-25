/* Network devices: the interface between NIC drivers and the TCP/IP stack. */
#ifndef OLUX_NETDEV_H
#define OLUX_NETDEV_H

#include <olux/list.h>
#include <olux/types.h>

struct net_device;

struct net_device_ops {
  /* Queue one Ethernet frame for transmission (copied). Called with the
   * stack lock held; may not sleep. Returns 0 or -errno. */
  int (*xmit)(struct net_device *d, const void *frame, size_t len);
  /* Deliver up to `budget` received frames with netdev_rx(); returns the
   * number delivered. Called from the netd thread with the lock held. */
  int (*poll)(struct net_device *d, int budget);
  /* Optional: update the hardware receive filter (promiscuous/all-multi). */
  void (*set_rx_mode)(struct net_device *d);
};

struct net_device {
  char name[16];
  u8 mac[6];
  u16 mtu;
  bool link_up;
  const struct net_device_ops *ops;
  void *priv;
  /* statistics */
  u64 rx_packets, tx_packets, rx_bytes, tx_bytes, rx_errors, tx_errors, rx_dropped, tx_dropped;
  /* stack state */
  void *netif; /* struct netif */
  volatile bool rx_pending;
  char hostname[64];
  struct list_head link;
};

/* Register a NIC; it is named ethN and comes up (DHCP is started by
 * userspace, see `netcfg`). */
int netdev_register(struct net_device *d);
/* From interrupt context: received frames are waiting; wake netd. */
void netdev_schedule(struct net_device *d);
/* From ops->poll: hand one received frame to the stack. */
void netdev_rx(struct net_device *d, const void *frame, size_t len);
void netdev_set_link(struct net_device *d, bool up);

/* The single lock around the TCP/IP stack. net_unlock() also delivers
 * packets looped back while it was held. */
void net_lock(void);
void net_unlock(void);
void net_kick(void); /* run the stack's timers soon */

long netdev_ioctl(unsigned cmd, u64 arg);

#endif
