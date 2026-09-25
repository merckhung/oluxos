/*
 * Network devices and the lwIP glue: one lock around the stack, the "netd"
 * kernel thread (driver receive polling and protocol timers), interface
 * configuration ioctls (SIOC*), DHCP control, and /proc/net/{dev,route}.
 */
#include <olux/device.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/net.h>
#include <olux/netdev.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/time.h>
#include <olux/uaccess.h>

#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/etharp.h"
#include "lwip/ethip6.h"
#include "lwip/igmp.h"
#include "lwip/init.h"
#include "lwip/nd6.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

extern char hostname[65];

static struct mutex stack_lock;
static struct thread *netd_thread;
static DEFINE_WAIT_QUEUE(netd_wq);
static volatile bool netd_work;
static LIST_HEAD(devices);
static int ndevices;
static bool stack_ready;

void net_lock(void) { mutex_lock(&stack_lock); }

void net_unlock(void) {
  netif_poll_all(); /* deliver looped-back packets queued meanwhile */
  mutex_unlock(&stack_lock);
}

void net_kick(void) {
  netd_work = true;
  wake_up(&netd_wq);
}

/* ---------------- lwIP netif glue ---------------- */

static err_t linkoutput(struct netif *nif, struct pbuf *p) {
  struct net_device *d = nif->state;
  static u8 frame[1600];
  if (p->tot_len > sizeof(frame)) {
    d->tx_dropped++;
    return ERR_BUF;
  }
  pbuf_copy_partial(p, frame, p->tot_len, 0);
  if (d->ops->xmit(d, frame, p->tot_len)) {
    d->tx_dropped++;
    return ERR_IF;
  }
  d->tx_packets++;
  d->tx_bytes += p->tot_len;
  return ERR_OK;
}

static err_t eth_netif_init(struct netif *nif) {
  struct net_device *d = nif->state;
  nif->name[0] = 'e';
  nif->name[1] = 't';
  nif->output = etharp_output;
  nif->output_ip6 = ethip6_output;
  nif->linkoutput = linkoutput;
  nif->mtu = d->mtu ? d->mtu : 1500;
  nif->hwaddr_len = ETH_HWADDR_LEN;
  memcpy(nif->hwaddr, d->mac, 6);
  nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP | NETIF_FLAG_MLD6;
  netif_set_hostname(nif, d->hostname);
  return ERR_OK;
}

void netdev_rx(struct net_device *d, const void *frame, size_t len) {
  struct netif *nif = d->netif;
  if (len > 1600 || !(nif->flags & NETIF_FLAG_UP)) {
    d->rx_dropped++;
    return;
  }
  struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
  if (!p) {
    d->rx_dropped++;
    return;
  }
  pbuf_take(p, frame, (u16_t)len);
  d->rx_packets++;
  d->rx_bytes += len;
  if (nif->input(p, nif) != ERR_OK) pbuf_free(p);
}

void netdev_schedule(struct net_device *d) {
  d->rx_pending = true;
  net_kick();
}

void netdev_set_link(struct net_device *d, bool up) {
  net_lock();
  d->link_up = up;
  if (up)
    netif_set_link_up(d->netif);
  else
    netif_set_link_down(d->netif);
  net_unlock();
}

static void net_stack_init(void);

void netdev_link_changed_locked(struct net_device *d) {
  if (d->link_up)
    netif_set_link_up(d->netif);
  else
    netif_set_link_down(d->netif);
}

int netdev_register(struct net_device *d) {
  net_stack_init(); /* NICs may probe before the initcalls run */
  struct netif *nif = kzalloc(sizeof(*nif), 0);
  if (!nif) return -ENOMEM;
  snprintf(d->name, sizeof(d->name), "eth%d", ndevices++);
  strlcpy(d->hostname, hostname, sizeof(d->hostname));
  d->netif = nif;
  net_lock();
  if (!netif_add(nif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, d, eth_netif_init, ethernet_input)) {
    net_unlock();
    kfree(nif);
    return -EIO;
  }
  list_add_tail(&d->link, &devices);
  if (!netif_default || netif_default == netif_find("lo0")) netif_set_default(nif);
  netif_create_ip6_linklocal_address(nif, 1);
  netif_set_ip6_autoconfig_enabled(nif, 1); /* SLAAC */
  netif_set_up(nif);
  if (d->link_up) netif_set_link_up(nif);
  net_unlock();
  pr_info("%s: %02x:%02x:%02x:%02x:%02x:%02x, MTU %u%s\n", d->name, d->mac[0], d->mac[1], d->mac[2], d->mac[3],
          d->mac[4], d->mac[5], nif->mtu, d->link_up ? ", link up" : "");
  return 0;
}

/* ---------------- netd ---------------- */

static int netd(void *arg) {
  for (;;) {
    wait_event_interruptible_timeout(netd_wq, netd_work, 20 * (long)NSEC_PER_MSEC);
    netd_work = false;
    net_lock();
    struct net_device *d;
    list_for_each_entry(d, &devices, link) {
      if (!d->rx_pending) continue;
      d->rx_pending = false;
      if (d->ops->poll(d, 64) >= 64) netd_work = true; /* more to do */
    }
    sys_check_timeouts();
    net_unlock();
  }
  return 0;
}

/* ---------------- interface ioctls ---------------- */

#define SIOCADDRT 0x890b
#define SIOCDELRT 0x890c
#define SIOCGIFNAME 0x8910
#define SIOCGIFCONF 0x8912
#define SIOCGIFFLAGS 0x8913
#define SIOCSIFFLAGS 0x8914
#define SIOCGIFADDR 0x8915
#define SIOCSIFADDR 0x8916
#define SIOCGIFDSTADDR 0x8917
#define SIOCGIFBRDADDR 0x8919
#define SIOCSIFBRDADDR 0x891a
#define SIOCGIFNETMASK 0x891b
#define SIOCSIFNETMASK 0x891c
#define SIOCGIFMETRIC 0x891d
#define SIOCGIFMTU 0x8921
#define SIOCSIFMTU 0x8922
#define SIOCSIFHWADDR 0x8924
#define SIOCGIFHWADDR 0x8927
#define SIOCGIFINDEX 0x8933
#define SIOCGIFTXQLEN 0x8942
#define SIOCOLUX_DHCP 0x89f0 /* ifr_ifindex: 1 start, 0 stop, 2 query (-> 1 if bound) */
#define SIOCOLUX_DNS 0x89f1  /* u32[3] <- DNS servers (network byte order) */

#define IFF_UP 0x1
#define IFF_BROADCAST 0x2
#define IFF_LOOPBACK 0x8
#define IFF_RUNNING 0x40
#define IFF_MULTICAST 0x1000

struct ifreq {
  char name[16];
  union {
    struct {
      u16 family;
      u16 port;
      u32 addr;
      u8 zero[8];
    } sin;
    struct {
      u16 family;
      u8 data[14];
    } hw;
    s16 flags;
    s32 ivalue;
    u64 ptr;
    u8 pad[24];
  };
};

struct ifconf {
  s32 len;
  u32 pad;
  u64 buf;
};

static bool is_loop(struct netif *nif) { return nif->name[0] == 'l' && nif->name[1] == 'o'; }

static struct net_device *dev_of(struct netif *nif) { return is_loop(nif) ? NULL : nif->state; }

static void if_name(struct netif *nif, char *out) {
  struct net_device *d = dev_of(nif);
  strlcpy(out, d ? d->name : "lo", 16);
}

static struct netif *find_if(const char *name) {
  struct netif *nif;
  NETIF_FOREACH(nif) {
    char n[16];
    if_name(nif, n);
    if (!strcmp(n, name)) return nif;
  }
  return NULL;
}

static short if_flags(struct netif *nif) {
  short f = 0;
  if (netif_is_up(nif)) f |= IFF_UP;
  if (netif_is_up(nif) && netif_is_link_up(nif)) f |= IFF_RUNNING;
  if (is_loop(nif))
    f |= IFF_LOOPBACK;
  else
    f |= IFF_BROADCAST | IFF_MULTICAST;
  return f;
}

static void set_sin(struct ifreq *r, u32 addr) {
  memset(&r->sin, 0, sizeof(r->sin));
  r->sin.family = AF_INET;
  r->sin.addr = addr;
}

static long ifreq_ioctl(unsigned cmd, struct ifreq *r) {
  r->name[15] = 0;
  struct netif *nif = find_if(r->name);
  if (cmd == SIOCGIFNAME) {
    NETIF_FOREACH(nif) {
      if (netif_get_index(nif) == r->ivalue) {
        if_name(nif, r->name);
        return 0;
      }
    }
    return -ENODEV;
  }
  if (!nif) return -ENODEV;
  bool root = current->proc->cred.euid == 0;
  ip4_addr_t a;
  switch (cmd) {
    case SIOCGIFFLAGS:
      r->flags = if_flags(nif);
      return 0;
    case SIOCSIFFLAGS:
      if (!root) return -EPERM;
      if (r->flags & IFF_UP)
        netif_set_up(nif);
      else if (!is_loop(nif)) {
        dhcp_release_and_stop(nif);
        netif_set_down(nif);
      }
      return 0;
    case SIOCGIFADDR:
      set_sin(r, ip4_addr_get_u32(netif_ip4_addr(nif)));
      return 0;
    case SIOCGIFNETMASK:
      set_sin(r, ip4_addr_get_u32(netif_ip4_netmask(nif)));
      return 0;
    case SIOCGIFBRDADDR:
      set_sin(r, ip4_addr_get_u32(netif_ip4_addr(nif)) | ~ip4_addr_get_u32(netif_ip4_netmask(nif)));
      return 0;
    case SIOCGIFDSTADDR:
      set_sin(r, 0);
      return 0;
    case SIOCSIFADDR:
    case SIOCSIFNETMASK:
      if (!root) return -EPERM;
      if (r->sin.family != AF_INET) return -EINVAL;
      ip4_addr_set_u32(&a, r->sin.addr);
      if (cmd == SIOCSIFADDR) {
        if (!is_loop(nif)) dhcp_release_and_stop(nif);
        netif_set_ipaddr(nif, &a);
        /* default mask for the address class, as ifconfig expects */
        if (ip4_addr_isany_val(*netif_ip4_netmask(nif))) {
          ip4_addr_t m;
          u32 h = lwip_ntohl(r->sin.addr);
          ip4_addr_set_u32(&m, lwip_htonl(h < 0x80000000u ? 0xff000000u : h < 0xc0000000u ? 0xffff0000u : 0xffffff00u));
          netif_set_netmask(nif, &m);
        }
      } else {
        netif_set_netmask(nif, &a);
      }
      return 0;
    case SIOCSIFBRDADDR:
      return root ? 0 : -EPERM;
    case SIOCGIFMTU:
      r->ivalue = nif->mtu;
      return 0;
    case SIOCSIFMTU:
      if (!root) return -EPERM;
      if (r->ivalue < 576 || r->ivalue > 1500) return -EINVAL;
      nif->mtu = (u16_t)r->ivalue;
      return 0;
    case SIOCGIFMETRIC:
      r->ivalue = 0;
      return 0;
    case SIOCGIFTXQLEN:
      r->ivalue = 1000;
      return 0;
    case SIOCGIFHWADDR:
      memset(&r->hw, 0, sizeof(r->hw));
      r->hw.family = is_loop(nif) ? 772 /* ARPHRD_LOOPBACK */ : 1 /* ARPHRD_ETHER */;
      memcpy(r->hw.data, nif->hwaddr, nif->hwaddr_len);
      return 0;
    case SIOCGIFINDEX:
      r->ivalue = netif_get_index(nif);
      return 0;
    case SIOCOLUX_DHCP:
      if (is_loop(nif)) return -EINVAL;
      if (r->ivalue == 2) {
        r->ivalue = dhcp_supplied_address(nif);
        return 0;
      }
      if (!root) return -EPERM;
      if (r->ivalue == 1) {
        struct net_device *d = dev_of(nif);
        strlcpy(d->hostname, hostname, sizeof(d->hostname));
        netif_set_up(nif);
        return dhcp_start(nif) == ERR_OK ? 0 : -ENOMEM;
      }
      dhcp_release_and_stop(nif);
      return 0;
    default:
      return -ENOTTY;
  }
}

struct rtentry {
  u64 pad1;
  struct {
    u16 family, port;
    u32 addr;
    u8 zero[8];
  } dst, gateway, genmask;
  u16 flags;
  s16 pad2;
  u64 pad3, pad4;
  s16 metric;
  u64 dev;
  u64 mtu, window;
  u16 irtt;
};

static long route_ioctl(unsigned cmd, u64 arg) {
  if (current->proc->cred.euid != 0) return -EPERM;
  struct rtentry rt;
  if (copy_from_user(&rt, arg, sizeof(rt))) return -EFAULT;
  if (rt.dst.addr != 0 || rt.genmask.addr != 0) return -EOPNOTSUPP; /* only the default route */
  struct netif *nif = NULL;
  if (rt.dev) {
    char name[16];
    if (strncpy_from_user(name, rt.dev, sizeof(name)) < 0) return -EFAULT;
    name[15] = 0;
    nif = find_if(name);
    if (!nif) return -ENODEV;
  }
  ip4_addr_t gw;
  ip4_addr_set_u32(&gw, rt.gateway.addr);
  if (!nif) { /* the interface whose subnet holds the gateway */
    struct netif *n;
    NETIF_FOREACH(n) {
      if (!is_loop(n) && ip4_addr_net_eq(&gw, netif_ip4_addr(n), netif_ip4_netmask(n))) nif = n;
    }
    if (!nif) return -ENETUNREACH;
  }
  if (cmd == SIOCADDRT) {
    netif_set_gw(nif, &gw);
    netif_set_default(nif);
  } else {
    netif_set_gw(nif, IP4_ADDR_ANY4);
  }
  return 0;
}

long netdev_ioctl(unsigned cmd, u64 arg) {
  long r;
  if (cmd == SIOCGIFCONF) {
    struct ifconf ic;
    if (copy_from_user(&ic, arg, sizeof(ic))) return -EFAULT;
    int n = 0;
    net_lock();
    struct netif *nif;
    NETIF_FOREACH(nif) {
      if (ic.buf && (n + 1) * (int)sizeof(struct ifreq) <= ic.len) {
        struct ifreq q = {0};
        if_name(nif, q.name);
        set_sin(&q, ip4_addr_get_u32(netif_ip4_addr(nif)));
        if (copy_to_user(ic.buf + n * sizeof(q), &q, sizeof(q))) {
          net_unlock();
          return -EFAULT;
        }
      }
      n++;
    }
    net_unlock();
    ic.len = ic.buf ? MIN(ic.len, n * (int)sizeof(struct ifreq)) : n * (int)sizeof(struct ifreq);
    return copy_to_user(arg, &ic, sizeof(ic));
  }
  if (cmd == SIOCADDRT || cmd == SIOCDELRT) {
    net_lock();
    r = route_ioctl(cmd, arg);
    net_unlock();
    return r;
  }
  if (cmd == SIOCOLUX_DNS) {
    u32 servers[DNS_MAX_SERVERS];
    net_lock();
    for (int i = 0; i < DNS_MAX_SERVERS; i++) {
      const ip_addr_t *a = dns_getserver((u8_t)i);
      servers[i] = IP_IS_V4(a) ? ip4_addr_get_u32(ip_2_ip4(a)) : 0;
    }
    net_unlock();
    return copy_to_user(arg, servers, sizeof(servers));
  }
  if ((cmd & 0xff00) != 0x8900) return -ENOTTY;
  struct ifreq req;
  if (copy_from_user(&req, arg, sizeof(req))) return -EFAULT;
  net_lock();
  r = ifreq_ioctl(cmd, &req);
  net_unlock();
  if (!r && copy_to_user(arg, &req, sizeof(req))) r = -EFAULT;
  return r;
}

/* ---------------- /proc/net ---------------- */

static void show_dev(seq_printf_t pr, void *ctx) {
  pr(ctx,
     "Inter-|   Receive                                                |  Transmit\n"
     " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls "
     "carrier compressed\n");
  net_lock();
  struct netif *nif;
  NETIF_FOREACH(nif) {
    struct net_device *d = dev_of(nif);
    char n[16];
    if_name(nif, n);
    if (d)
      pr(ctx, "%6s: %llu %llu %llu %llu 0 0 0 0 %llu %llu %llu %llu 0 0 0 0\n", n, (unsigned long long)d->rx_bytes,
         (unsigned long long)d->rx_packets, (unsigned long long)d->rx_errors, (unsigned long long)d->rx_dropped,
         (unsigned long long)d->tx_bytes, (unsigned long long)d->tx_packets, (unsigned long long)d->tx_errors,
         (unsigned long long)d->tx_dropped);
    else
      pr(ctx, "%6s: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n", n);
  }
  net_unlock();
}

static void show_route(seq_printf_t pr, void *ctx) {
  pr(ctx, "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n");
  net_lock();
  struct netif *nif;
  NETIF_FOREACH(nif) {
    if (is_loop(nif) || !netif_is_up(nif) || ip4_addr_isany_val(*netif_ip4_addr(nif))) continue;
    char n[16];
    if_name(nif, n);
    u32 ip = ip4_addr_get_u32(netif_ip4_addr(nif)), mask = ip4_addr_get_u32(netif_ip4_netmask(nif));
    u32 gw = ip4_addr_get_u32(netif_ip4_gw(nif));
    if (nif == netif_default && gw) pr(ctx, "%s\t%08X\t%08X\t0003\t0\t0\t0\t%08X\t0\t0\t0\n", n, 0u, gw, 0u);
    pr(ctx, "%s\t%08X\t%08X\t0001\t0\t0\t0\t%08X\t0\t0\t0\n", n, ip & mask, 0u, mask);
  }
  net_unlock();
}

static void show_if_inet6(seq_printf_t pr, void *ctx) {
  net_lock();
  struct netif *nif;
  NETIF_FOREACH(nif) {
    char n[16];
    if_name(nif, n);
    for (int k = 0; k < LWIP_IPV6_NUM_ADDRESSES; k++) {
      if (!ip6_addr_isvalid(netif_ip6_addr_state(nif, k))) continue;
      const ip6_addr_t *a = netif_ip6_addr(nif, k);
      u32 w[4];
      memcpy(w, a->addr, 16);
      int scope = ip6_addr_isloopback(a) ? 0x10 : ip6_addr_islinklocal(a) ? 0x20 : 0x00;
      pr(ctx, "%08x%08x%08x%08x %02x %02x %02x %02x %8s\n", lwip_ntohl(w[0]), lwip_ntohl(w[1]), lwip_ntohl(w[2]),
         lwip_ntohl(w[3]), netif_get_index(nif), ip6_addr_isloopback(a) ? 128 : 64, scope,
         ip6_addr_istentative(netif_ip6_addr_state(nif, k)) ? 0x40 : 0x80, n);
    }
  }
  net_unlock();
}

/* ---------------- init ---------------- */

static void net_stack_init(void) {
  if (stack_ready) return;
  mutex_init(&stack_lock);
  lwip_init();
  stack_ready = true;
  netd_thread = kthread_create(netd, NULL, "netd");
  if (netd_thread) sched_add_new(netd_thread);
  proc_net_register("dev", show_dev);
  proc_net_register("route", show_route);
  proc_net_register("if_inet6", show_if_inet6);
}

static int net_init(void) {
  net_stack_init();
  return 0;
}
core_initcall(net_init);
