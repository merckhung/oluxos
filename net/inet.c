/*
 * AF_INET sockets on lwIP's raw API: SOCK_STREAM (TCP), SOCK_DGRAM (UDP)
 * and SOCK_RAW (e.g. ICMP for ping). Also /proc/net/{tcp,udp}.
 *
 * lwIP callbacks run with the stack lock held (from netd or from a system
 * call); they only queue data, record state and wake the socket's wait
 * queue. User memory is copied outside the lock.
 */
#include <olux/device.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/net.h>
#include <olux/netdev.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/uaccess.h>

#include "lwip/igmp.h"
#include "lwip/ip.h"
#include "lwip/ip4.h"
#include "lwip/mld6.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/raw.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"

#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IP_TOS 1
#define IP_TTL 2
#define IP_HDRINCL 3
#define IP_PKTINFO 8
#define IP_RECVERR 11
#define IP_MULTICAST_IF 32
#define IP_MULTICAST_TTL 33
#define IP_MULTICAST_LOOP 34
#define IP_ADD_MEMBERSHIP 35
#define IP_DROP_MEMBERSHIP 36
#define TCP_NODELAY 1
#define TCP_MAXSEG 2
#define TCP_KEEPIDLE 4
#define TCP_KEEPINTVL 5
#define TCP_KEEPCNT 6
#define TCP_INFO 11
#define FIONREAD 0x541b
#define TIOCOUTQ 0x5411
#define CHUNK 16384
#define IPPROTO_ICMPV6 58
#define IPPROTO_IPV6 41
#define IPV6_CHECKSUM 7
#define IPV6_UNICAST_HOPS 16
#define IPV6_MULTICAST_HOPS 18
#define IPV6_MULTICAST_LOOP 19
#define IPV6_JOIN_GROUP 20
#define IPV6_LEAVE_GROUP 21
#define IPV6_V6ONLY 26
#define IPV6_RECVPKTINFO 49
#define IPV6_RECVHOPLIMIT 51
#define IPV6_TCLASS 67

enum { S_CLOSED, S_BOUND, S_LISTEN, S_CONNECTING, S_CONNECTED };

struct dgram {
  struct list_head link;
  struct pbuf *p;
  ip_addr_t addr;
  u16 port; /* host order */
};

struct isock {
  struct socket *sock;
  int state;
  union {
    struct tcp_pcb *tcp;
    struct udp_pcb *udp;
    struct raw_pcb *raw;
    void *any;
  } pcb;
  /* TCP */
  struct pbuf *rxq;
  size_t rx_avail;
  bool rx_eof, shut_rd, shut_wr;
  int err; /* pending asynchronous error (negative errno) */
  struct list_head acceptq;
  int nacc, backlog;
  struct list_head acc_link;
  bool nodelay;
  /* UDP / raw */
  struct list_head dgq;
  size_t dgq_bytes;
  bool hdrincl;
  bool v6only;
  bool has_peer; /* connected raw socket */
  ip_addr_t peer;
};

static struct isock *is(struct socket *s) { return s->priv; }

static int lwip_errno(err_t e) {
  switch (e) {
    case ERR_OK:
      return 0;
    case ERR_MEM:
    case ERR_BUF:
      return -ENOBUFS;
    case ERR_TIMEOUT:
      return -ETIMEDOUT;
    case ERR_RTE:
      return -ENETUNREACH;
    case ERR_INPROGRESS:
      return -EINPROGRESS;
    case ERR_VAL:
      return -EINVAL;
    case ERR_WOULDBLOCK:
      return -EAGAIN;
    case ERR_USE:
      return -EADDRINUSE;
    case ERR_ALREADY:
      return -EALREADY;
    case ERR_ISCONN:
      return -EISCONN;
    case ERR_CONN:
      return -ENOTCONN;
    case ERR_IF:
      return -ENETDOWN;
    case ERR_ABRT:
      return -ECONNABORTED;
    case ERR_RST:
      return -ECONNRESET;
    case ERR_CLSD:
      return -ENOTCONN;
    case ERR_ARG:
      return -EINVAL;
    default:
      return -EIO;
  }
}

struct sin {
  u16 family;
  u16 port; /* network order */
  u32 addr; /* network order */
  u8 zero[8];
};

struct sin6 {
  u16 family;
  u16 port;
  u32 flowinfo;
  u8 addr[16];
  u32 scope_id;
};

/* Socket address -> lwIP address. AF_INET6 sockets are dual-stack: a
 * v4-mapped address (::ffff:a.b.c.d) means IPv4. */
static int get_addr(struct socket *s, const void *a, u32 len, ip_addr_t *ip, u16 *port) {
  u16 fam = len >= 2 ? *(const u16 *)a : 0;
  if (s->family == AF_INET) {
    const struct sin *in = a;
    if (len < 8 || (fam != AF_INET && fam != AF_UNSPEC)) return -EAFNOSUPPORT;
    ip_addr_set_ip4_u32_val(*ip, in->addr);
    *port = lwip_ntohs(in->port);
    return 0;
  }
  if (fam == AF_INET && len >= 8) { /* tolerated, as on Linux for connect() */
    const struct sin *in = a;
    ip_addr_set_ip4_u32_val(*ip, in->addr);
    *port = lwip_ntohs(in->port);
    return 0;
  }
  const struct sin6 *in6 = a;
  if (len < 24 || fam != AF_INET6) return -EAFNOSUPPORT;
  *port = lwip_ntohs(in6->port);
  u32 w[4];
  memcpy(w, in6->addr, 16);
  if (w[0] == 0 && w[1] == 0 && w[2] == lwip_htonl(0xffff)) {
    ip_addr_set_ip4_u32_val(*ip, w[3]);
    return 0;
  }
  IP_ADDR6(ip, w[0], w[1], w[2], w[3]);
  if (ip6_addr_islinklocal(ip_2_ip6(ip))) {
    struct netif *nif = len >= 28 && in6->scope_id ? netif_get_by_index((u8_t)in6->scope_id) : netif_default;
    if (nif) ip6_addr_assign_zone(ip_2_ip6(ip), IP6_UNICAST, nif);
  }
  return 0;
}

static void put_addr(struct socket *s, void *a, u32 *len, const ip_addr_t *ip, u16 port) {
  if (s->family == AF_INET) {
    struct sin in = {AF_INET, lwip_htons(port), IP_IS_V4(ip) ? ip4_addr_get_u32(ip_2_ip4(ip)) : 0, {0}};
    memcpy(a, &in, sizeof(in));
    *len = sizeof(in);
    return;
  }
  struct sin6 in6 = {AF_INET6, lwip_htons(port), 0, {0}, 0};
  if (IP_IS_V4(ip)) {
    u32 w[4] = {0, 0, lwip_htonl(0xffff), ip4_addr_get_u32(ip_2_ip4(ip))};
    memcpy(in6.addr, w, 16);
  } else {
    memcpy(in6.addr, ip_2_ip6(ip)->addr, 16);
    if (ip6_addr_has_zone(ip_2_ip6(ip))) in6.scope_id = ip6_addr_zone(ip_2_ip6(ip));
  }
  memcpy(a, &in6, sizeof(in6));
  *len = sizeof(in6);
}

/* ---------------- TCP callbacks (stack lock held) ---------------- */

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
  struct isock *i = arg;
  if (!i) {
    if (p) pbuf_free(p);
    return ERR_OK;
  }
  if (!p) {
    i->rx_eof = true;
  } else if (i->shut_rd) {
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
  } else {
    if (i->rxq)
      pbuf_cat(i->rxq, p);
    else
      i->rxq = p;
    i->rx_avail += p->tot_len;
  }
  wake_up(&i->sock->wq);
  return ERR_OK;
}

static err_t tcp_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len) {
  struct isock *i = arg;
  if (i) wake_up(&i->sock->wq);
  return ERR_OK;
}

static void tcp_err_cb(void *arg, err_t err) {
  struct isock *i = arg; /* the pcb is already freed */
  if (!i) return;
  i->pcb.tcp = NULL;
  int e = lwip_errno(err);
  if (i->state == S_CONNECTING && (err == ERR_RST || err == ERR_ABRT)) e = -ECONNREFUSED;
  i->err = e;
  i->sock->so_error = e;
  i->state = S_CLOSED;
  i->rx_eof = true;
  wake_up(&i->sock->wq);
}

static err_t tcp_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err) {
  struct isock *i = arg;
  if (i) {
    i->state = S_CONNECTED;
    wake_up(&i->sock->wq);
  }
  return ERR_OK;
}

static void tcp_setup(struct isock *i, struct tcp_pcb *pcb) {
  i->pcb.tcp = pcb;
  tcp_arg(pcb, i);
  tcp_recv(pcb, tcp_recv_cb);
  tcp_sent(pcb, tcp_sent_cb);
  tcp_err(pcb, tcp_err_cb);
}

static const struct proto_ops inet_ops;
static struct isock *isock_new(struct socket *s);

static err_t tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
  struct isock *l = arg;
  if (!l || err != ERR_OK || !newpcb) return ERR_VAL;
  if (l->nacc >= l->backlog) {
    tcp_abort(newpcb);
    return ERR_ABRT;
  }
  struct socket *ns = sock_alloc(l->sock->family, SOCK_STREAM, IPPROTO_TCP);
  struct isock *ni = ns ? isock_new(ns) : NULL;
  if (!ni) {
    kfree(ns);
    tcp_abort(newpcb);
    return ERR_ABRT;
  }
  ns->ops = &inet_ops;
  ns->owner = l->sock->owner;
  tcp_setup(ni, newpcb);
  ni->state = S_CONNECTED;
  if (l->nodelay) tcp_nagle_disable(newpcb);
  tcp_backlog_delayed(newpcb);
  list_add_tail(&ni->acc_link, &l->acceptq);
  l->nacc++;
  wake_up(&l->sock->wq);
  return ERR_OK;
}

/* ---------------- UDP / raw receive (stack lock held) ---------------- */

static void queue_dgram(struct isock *i, struct pbuf *p, const ip_addr_t *addr, u16 port) {
  if (i->dgq_bytes + p->tot_len > i->sock->rcvbuf) {
    pbuf_free(p);
    return;
  }
  struct dgram *d = kmalloc(sizeof(*d), 0);
  if (!d) {
    pbuf_free(p);
    return;
  }
  d->p = p;
  ip_addr_copy(d->addr, *addr);
  d->port = port;
  list_add_tail(&d->link, &i->dgq);
  i->dgq_bytes += p->tot_len;
  wake_up(&i->sock->wq);
}

static void udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
  struct isock *i = arg;
  if (!i || i->shut_rd) {
    pbuf_free(p);
    return;
  }
  queue_dgram(i, p, addr, port);
}

static u8_t raw_recv_cb(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
  struct isock *i = arg;
  if (i && !i->shut_rd && (!i->has_peer || ip_addr_eq(&i->peer, addr))) {
    /* copy: the packet continues to the stack (e.g. ICMP echo handling) */
    struct pbuf *c = pbuf_clone(PBUF_RAW, PBUF_RAM, p);
    /* like Linux, IPv6 raw sockets see the payload without the IP header */
    if (c && IP_IS_V6(addr)) pbuf_remove_header(c, ip_current_header_tot_len());
    if (c) queue_dgram(i, c, addr, 0);
  }
  return 0; /* not eaten */
}

/* ---------------- socket operations ---------------- */

static struct isock *isock_new(struct socket *s) {
  struct isock *i = kzalloc(sizeof(*i), 0);
  if (!i) return NULL;
  i->sock = s;
  list_init(&i->acceptq);
  list_init(&i->dgq);
  list_init(&i->acc_link);
  s->priv = i;
  return i;
}

static void flush_dgq(struct isock *i) {
  while (!list_empty(&i->dgq)) {
    struct dgram *d = list_first_entry(&i->dgq, struct dgram, link);
    list_del(&d->link);
    pbuf_free(d->p);
    kfree(d);
  }
  i->dgq_bytes = 0;
}

/* tcp_close() failed for lack of memory (full send queue): retry from the
 * poll timer (every second) rather than resetting the connection, which
 * would throw away data the application already wrote. Give up after about
 * a minute. The pcb has no socket any more; its arg counts the attempts. */
static err_t close_retry(void *arg, struct tcp_pcb *pcb) {
  uintptr_t tries = (uintptr_t)arg + 1;
  if (tcp_close(pcb) == ERR_OK) return ERR_OK;
  if (tries > 60) {
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  tcp_arg(pcb, (void *)tries);
  return ERR_OK;
}

static int in_release(struct socket *s) {
  struct isock *i = is(s);
  if (!i) return 0;
  net_lock();
  /* connections that were never accepted */
  while (!list_empty(&i->acceptq)) {
    struct isock *e = list_first_entry(&i->acceptq, struct isock, acc_link);
    list_del(&e->acc_link);
    list_init(&e->acc_link);
    net_unlock();
    sock_free(e->sock);
    net_lock();
  }
  switch (s->type) {
    case SOCK_STREAM:
      if (i->pcb.tcp) {
        struct tcp_pcb *pcb = i->pcb.tcp;
        tcp_arg(pcb, NULL);
        if (i->state != S_LISTEN) {
          tcp_recv(pcb, NULL);
          tcp_sent(pcb, NULL);
          tcp_err(pcb, NULL);
        }
        if (i->state == S_CONNECTED && i->rx_avail)
          tcp_abort(pcb); /* unread data: reset, like Linux */
        else if (tcp_close(pcb) != ERR_OK)
          tcp_poll(pcb, close_retry, 2); /* no memory for the FIN yet: queued data still goes out */
      }
      if (i->rxq) pbuf_free(i->rxq);
      break;
    case SOCK_DGRAM:
      if (i->pcb.udp) udp_remove(i->pcb.udp);
      break;
    case SOCK_RAW:
      if (i->pcb.raw) raw_remove(i->pcb.raw);
      break;
  }
  flush_dgq(i);
  net_unlock();
  kfree(i);
  s->priv = NULL;
  return 0;
}

static void apply_reuse(struct socket *s) {
  struct isock *i = is(s);
  if (!i->pcb.any) return;
  if (s->type == SOCK_STREAM) {
    if (s->reuseaddr) ip_set_option(i->pcb.tcp, SOF_REUSEADDR);
  } else if (s->type == SOCK_DGRAM) {
    if (s->reuseaddr) ip_set_option(i->pcb.udp, SOF_REUSEADDR);
    if (s->broadcast) ip_set_option(i->pcb.udp, SOF_BROADCAST);
  }
}

static int in_bind(struct socket *s, const void *addr, u32 len) {
  struct isock *i = is(s);
  ip_addr_t ip;
  u16 port;
  int r = get_addr(s, addr, len, &ip, &port);
  if (r) return r;
  if (port && port < 1024 && current->proc->cred.euid != 0) return -EACCES;
  /* "::" on a dual-stack socket also accepts IPv4 (like Linux without IPV6_V6ONLY) */
  if (s->family == AF_INET6 && !i->v6only && s->type != SOCK_RAW && IP_IS_V6(&ip) && ip_addr_isany(&ip))
    ip_addr_copy(ip, *IP_ANY_TYPE);
  net_lock();
  apply_reuse(s);
  err_t e = ERR_OK;
  switch (s->type) {
    case SOCK_STREAM:
      if (!i->pcb.tcp || i->state != S_CLOSED)
        e = ERR_VAL;
      else
        e = tcp_bind(i->pcb.tcp, &ip, port);
      break;
    case SOCK_DGRAM:
      e = udp_bind(i->pcb.udp, &ip, port);
      break;
    case SOCK_RAW:
      e = raw_bind(i->pcb.raw, &ip);
      break;
  }
  if (e == ERR_OK && s->type == SOCK_STREAM) i->state = S_BOUND;
  net_unlock();
  return e == ERR_VAL && s->type == SOCK_STREAM ? -EINVAL : lwip_errno(e);
}

static int in_listen(struct socket *s, int backlog) {
  struct isock *i = is(s);
  if (s->type != SOCK_STREAM) return -EOPNOTSUPP;
  net_lock();
  int r = 0;
  if (i->state == S_LISTEN) {
    i->backlog = backlog;
  } else if (i->state != S_CLOSED && i->state != S_BOUND) {
    r = -EINVAL;
  } else {
    if (i->state == S_CLOSED) { /* listen on an ephemeral port */
      apply_reuse(s);
      if (tcp_bind(i->pcb.tcp, s->family == AF_INET ? IP4_ADDR_ANY : IP_ANY_TYPE, 0) != ERR_OK) r = -EADDRINUSE;
    }
    struct tcp_pcb *l = r ? NULL : tcp_listen_with_backlog(i->pcb.tcp, (u8_t)MIN(backlog, 255));
    if (!r && !l) r = -ENOBUFS;
    if (!r) {
      i->pcb.tcp = l; /* the old pcb has been freed */
      tcp_arg(l, i);
      tcp_accept(l, tcp_accept_cb);
      i->state = S_LISTEN;
      i->backlog = backlog;
    }
  }
  net_unlock();
  return r;
}

static int in_accept(struct socket *s, struct socket **out) {
  struct isock *i = is(s);
  if (i->state != S_LISTEN) return -EINVAL;
  for (;;) {
    net_lock();
    if (!list_empty(&i->acceptq)) {
      struct isock *e = list_first_entry(&i->acceptq, struct isock, acc_link);
      list_del(&e->acc_link);
      list_init(&e->acc_link);
      i->nacc--;
      if (e->pcb.tcp) tcp_backlog_accepted(e->pcb.tcp);
      net_unlock();
      *out = e->sock;
      return 0;
    }
    net_unlock();
    int r = sock_wait(s, !list_empty(&i->acceptq) || i->state != S_LISTEN, sock_nonblock(s, 0), s->rcvtimeo_ns);
    if (r) return r;
    if (i->state != S_LISTEN) return -EINVAL;
  }
}

static int in_connect(struct socket *s, const void *addr, u32 len) {
  struct isock *i = is(s);
  ip_addr_t ip;
  u16 port;
  if (len >= 2 && *(const u16 *)addr == AF_UNSPEC && s->type != SOCK_STREAM) {
    net_lock();
    if (s->type == SOCK_DGRAM)
      udp_disconnect(i->pcb.udp);
    else
      i->has_peer = false;
    net_unlock();
    return 0;
  }
  int r = get_addr(s, addr, len, &ip, &port);
  if (r) return r;
  if (s->type == SOCK_DGRAM) {
    net_lock();
    err_t e = udp_connect(i->pcb.udp, &ip, port);
    net_unlock();
    return lwip_errno(e);
  }
  if (s->type == SOCK_RAW) {
    ip_addr_copy(i->peer, ip);
    i->has_peer = true;
    return 0;
  }
  net_lock();
  if (i->state == S_CONNECTED)
    r = -EISCONN;
  else if (i->state == S_CONNECTING)
    r = -EALREADY;
  else if (i->state == S_LISTEN || !i->pcb.tcp)
    r = -EINVAL;
  if (!r) {
    apply_reuse(s);
    if (i->nodelay) tcp_nagle_disable(i->pcb.tcp);
    if (s->keepalive) ip_set_option(i->pcb.tcp, SOF_KEEPALIVE);
    i->err = 0;
    err_t e = tcp_connect(i->pcb.tcp, &ip, port, tcp_connected_cb);
    r = lwip_errno(e);
    if (!r) i->state = S_CONNECTING;
  }
  net_unlock();
  if (r) return r;
  if (sock_nonblock(s, 0)) return -EINPROGRESS;
  r = sock_wait(s, i->state != S_CONNECTING, false, s->sndtimeo_ns);
  if (r == -EAGAIN) return -ETIMEDOUT;
  if (r) return r == -ERESTARTSYS ? -EINTR : r; /* connect is not restarted */
  if (i->state == S_CONNECTED) return 0;
  s->so_error = 0;
  return i->err ? i->err : -ECONNREFUSED;
}

static ssize_t tcp_send(struct socket *s, struct kmsg *m, int flags) {
  struct isock *i = is(s);
  size_t done = 0;
  u8 *buf = kmalloc(CHUNK, 0);
  if (!buf) return -ENOMEM;
  ssize_t r = 0;
  while (done < m->len) {
    size_t n = MIN(m->len - done, (size_t)CHUNK);
    if (kmsg_copy_from(m, done, buf, n)) {
      r = -EFAULT;
      break;
    }
    size_t off = 0;
    while (off < n) {
      net_lock();
      if (i->err || i->state != S_CONNECTED || i->shut_wr || !i->pcb.tcp) {
        int e = i->err ? i->err : (i->state == S_CONNECTING ? -EAGAIN : i->state == S_CONNECTED ? -EPIPE : -ENOTCONN);
        if (i->state == S_CLOSED && !i->err) e = -EPIPE;
        net_unlock();
        r = e;
        goto out;
      }
      u32 room = tcp_sndbuf(i->pcb.tcp);
      if (tcp_sndqueuelen(i->pcb.tcp) >= TCP_SND_QUEUELEN - 4) room = 0;
      if (room) {
        u32 w = (u32)MIN((size_t)room, n - off);
        u8_t fl = TCP_WRITE_FLAG_COPY | (done + off + w < m->len ? TCP_WRITE_FLAG_MORE : 0);
        err_t e = tcp_write(i->pcb.tcp, buf + off, (u16_t)MIN(w, 0xffffu), fl);
        if (e == ERR_OK) {
          off += MIN(w, 0xffffu);
          tcp_output(i->pcb.tcp);
          net_unlock();
          continue;
        }
        if (e != ERR_MEM) {
          net_unlock();
          r = lwip_errno(e);
          goto out;
        }
      }
      tcp_output(i->pcb.tcp);
      net_unlock();
      if ((done + off) && sock_nonblock(s, flags)) {
        done += off;
        goto out;
      }
      int w = sock_wait(s,
                        i->err || i->state != S_CONNECTED || !i->pcb.tcp ||
                            (tcp_sndbuf(i->pcb.tcp) > 0 && tcp_sndqueuelen(i->pcb.tcp) < TCP_SND_QUEUELEN - 4),
                        sock_nonblock(s, flags), s->sndtimeo_ns);
      if (w) {
        done += off;
        r = w;
        goto out;
      }
    }
    done += n;
  }
out:
  kfree(buf);
  if (done) return done;
  return r;
}

static ssize_t tcp_recvmsg(struct socket *s, struct kmsg *m, int flags) {
  struct isock *i = is(s);
  if (i->state == S_LISTEN) return -ENOTCONN;
  size_t done = 0;
  u8 *buf = kmalloc(CHUNK, 0);
  if (!buf) return -ENOMEM;
  ssize_t r = 0;
  while (done < m->len) {
    net_lock();
    if (!i->rx_avail) {
      bool eof = i->rx_eof || i->shut_rd;
      int err = i->err;
      bool nc = i->state != S_CONNECTED && i->state != S_CLOSED && !eof;
      net_unlock();
      if (done && (!(flags & MSG_WAITALL) || eof || err)) break;
      if (err && !done) {
        r = err;
        i->err = 0;
        break;
      }
      if (eof) break;
      if (nc && i->state != S_CONNECTING) {
        r = -ENOTCONN;
        break;
      }
      int w = sock_wait(s, i->rx_avail || i->rx_eof || i->err || i->shut_rd, sock_nonblock(s, flags), s->rcvtimeo_ns);
      if (w) {
        if (!done) r = w;
        break;
      }
      continue;
    }
    size_t n = MIN(MIN(i->rx_avail, m->len - done), (size_t)CHUNK);
    pbuf_copy_partial(i->rxq, buf, (u16_t)n, 0);
    if (!(flags & MSG_PEEK)) {
      i->rxq = pbuf_free_header(i->rxq, (u16_t)n);
      i->rx_avail -= n;
      if (i->pcb.tcp) tcp_recved(i->pcb.tcp, (u16_t)n);
    }
    net_unlock();
    if (kmsg_copy_to(m, done, buf, n)) {
      if (!done) r = -EFAULT;
      break;
    }
    done += n;
    if (flags & MSG_PEEK) break;
  }
  kfree(buf);
  return done ? (ssize_t)done : r;
}

static ssize_t dgram_send(struct socket *s, struct kmsg *m, int flags) {
  struct isock *i = is(s);
  if (m->len > 65507) return -EMSGSIZE;
  struct pbuf *p = pbuf_alloc(s->type == SOCK_RAW ? PBUF_IP : PBUF_TRANSPORT, (u16_t)m->len, PBUF_RAM);
  if (!p) return -ENOBUFS;
  if (m->len && kmsg_copy_from(m, 0, p->payload, m->len)) {
    pbuf_free(p);
    return -EFAULT;
  }
  ip_addr_t ip;
  u16 port = 0;
  bool to = m->namelen;
  if (to) {
    int r = get_addr(s, m->name, m->namelen, &ip, &port);
    if (r) {
      pbuf_free(p);
      return r;
    }
  }
  net_lock();
  err_t e;
  if (s->type == SOCK_DGRAM) {
    if (!to && !(i->pcb.udp->flags & UDP_FLAGS_CONNECTED))
      e = ERR_CONN;
    else if (to)
      e = udp_sendto(i->pcb.udp, p, &ip, port);
    else
      e = udp_send(i->pcb.udp, p);
  } else {
    if (!to && !i->has_peer)
      e = ERR_CONN;
    else {
      if (!to) ip_addr_copy(ip, i->peer);
      e = raw_sendto(i->pcb.raw, p, &ip);
    }
  }
  net_unlock();
  pbuf_free(p);
  if (e == ERR_CONN) return -EDESTADDRREQ;
  return e == ERR_OK ? (ssize_t)m->len : lwip_errno(e);
}

static ssize_t dgram_recv(struct socket *s, struct kmsg *m, int flags) {
  struct isock *i = is(s);
  for (;;) {
    net_lock();
    if (!list_empty(&i->dgq)) {
      struct dgram *d = list_first_entry(&i->dgq, struct dgram, link);
      if (!(flags & MSG_PEEK)) {
        list_del(&d->link);
        i->dgq_bytes -= d->p->tot_len;
      }
      net_unlock();
      size_t full = d->p->tot_len, n = MIN(full, m->len);
      u8 *buf = n ? kmalloc(n, 0) : NULL;
      ssize_t r = 0;
      if (n && !buf) r = -ENOMEM;
      if (!r && n) {
        pbuf_copy_partial(d->p, buf, (u16_t)n, 0);
        if (kmsg_copy_to(m, 0, buf, n)) r = -EFAULT;
      }
      kfree(buf);
      put_addr(s, m->name, &m->namelen, &d->addr, d->port);
      if (n < full) m->flags |= MSG_TRUNC;
      if (!(flags & MSG_PEEK)) {
        net_lock();
        pbuf_free(d->p);
        net_unlock();
        kfree(d);
      }
      if (r) return r;
      return (flags & MSG_TRUNC) ? (ssize_t)full : (ssize_t)n;
    }
    bool shut = i->shut_rd;
    net_unlock();
    if (shut) return 0;
    int w = sock_wait(s, !list_empty(&i->dgq) || i->shut_rd, sock_nonblock(s, flags), s->rcvtimeo_ns);
    if (w) return w;
  }
}

static ssize_t in_sendmsg(struct socket *s, struct kmsg *m, int flags) {
  if (m->nfds) return -EOPNOTSUPP;
  if (s->type == SOCK_STREAM) {
    if (m->namelen && is(s)->state != S_CONNECTED) return -ENOTCONN;
    return tcp_send(s, m, flags);
  }
  return dgram_send(s, m, flags);
}

static ssize_t in_recvmsg(struct socket *s, struct kmsg *m, int flags) {
  if (flags & MSG_OOB) return -EOPNOTSUPP;
  return s->type == SOCK_STREAM ? tcp_recvmsg(s, m, flags) : dgram_recv(s, m, flags);
}

static int in_shutdown(struct socket *s, int how) {
  struct isock *i = is(s);
  net_lock();
  int r = 0;
  if (s->type == SOCK_STREAM && i->state != S_CONNECTED && i->state != S_LISTEN) r = -ENOTCONN;
  if (!r) {
    if (how != SHUT_WR) i->shut_rd = true;
    if (how != SHUT_RD) i->shut_wr = true;
    if (s->type == SOCK_STREAM && i->state == S_CONNECTED && i->pcb.tcp) {
      if (how != SHUT_RD) tcp_shutdown(i->pcb.tcp, 0, 1);
      if (how != SHUT_WR && i->rxq) { /* discard what is queued */
        tcp_recved(i->pcb.tcp, (u16_t)MIN(i->rx_avail, 0xffffu));
        pbuf_free(i->rxq);
        i->rxq = NULL;
        i->rx_avail = 0;
      }
    }
    wake_up(&s->wq);
  }
  net_unlock();
  return r;
}

static int in_getname(struct socket *s, void *addr, u32 *len, bool peer) {
  struct isock *i = is(s);
  net_lock();
  int r = 0;
  ip_addr_t a;
  ip_addr_set_zero(&a);
  if (s->family == AF_INET) ip_addr_set_zero_ip4(&a);
  u16 port = 0;
  switch (s->type) {
    case SOCK_STREAM:
      if (!i->pcb.tcp) {
        if (peer) r = -ENOTCONN;
      } else if (peer) {
        if (i->state != S_CONNECTED) r = -ENOTCONN;
        ip_addr_copy(a, i->pcb.tcp->remote_ip);
        port = i->pcb.tcp->remote_port;
      } else {
        ip_addr_copy(a, i->pcb.tcp->local_ip);
        port = i->pcb.tcp->local_port;
      }
      break;
    case SOCK_DGRAM:
      if (peer) {
        if (!(i->pcb.udp->flags & UDP_FLAGS_CONNECTED)) r = -ENOTCONN;
        ip_addr_copy(a, i->pcb.udp->remote_ip);
        port = i->pcb.udp->remote_port;
      } else {
        ip_addr_copy(a, i->pcb.udp->local_ip);
        port = i->pcb.udp->local_port;
      }
      break;
    default:
      if (peer && !i->has_peer) r = -ENOTCONN;
      if (peer)
        ip_addr_copy(a, i->peer);
      else
        ip_addr_copy(a, i->pcb.raw->local_ip);
  }
  net_unlock();
  if (IP_IS_ANY_TYPE_VAL(a)) IP_SET_TYPE_VAL(a, s->family == AF_INET ? IPADDR_TYPE_V4 : IPADDR_TYPE_V6);
  if (!r) put_addr(s, addr, len, &a, port);
  return r;
}

static int getint(const void *val, u32 len, int *out) {
  if (len < 4) {
    if (len < 1) return -EINVAL;
    *out = *(const u8 *)val;
    return 0;
  }
  memcpy(out, val, 4);
  return 0;
}

static int in_setsockopt(struct socket *s, int level, int opt, const void *val, u32 len) {
  struct isock *i = is(s);
  int v = 0, r = 0;
  if (level == SOL_SOCKET) {
    if (opt != SO_KEEPALIVE && opt != SO_REUSEADDR && opt != SO_BROADCAST) return -ENOPROTOOPT;
    if (getint(val, len, &v)) return -EINVAL;
    net_lock();
    if (opt == SO_KEEPALIVE && s->type == SOCK_STREAM && i->pcb.tcp) {
      if (v)
        ip_set_option(i->pcb.tcp, SOF_KEEPALIVE);
      else
        ip_reset_option(i->pcb.tcp, SOF_KEEPALIVE);
    }
    net_unlock();
    return -ENOPROTOOPT; /* let the generic layer record it too */
  }
  if (level == IPPROTO_TCP) {
    if (s->type != SOCK_STREAM) return -ENOPROTOOPT;
    if (getint(val, len, &v)) return -EINVAL;
    net_lock();
    struct tcp_pcb *p = i->pcb.tcp;
    switch (opt) {
      case TCP_NODELAY:
        i->nodelay = v != 0;
        if (p && i->state != S_LISTEN) {
          if (v)
            tcp_nagle_disable(p);
          else
            tcp_nagle_enable(p);
        }
        break;
      case TCP_KEEPIDLE:
        if (p && i->state != S_LISTEN) p->keep_idle = (u32_t)v * 1000;
        break;
      case TCP_KEEPINTVL:
        if (p && i->state != S_LISTEN) p->keep_intvl = (u32_t)v * 1000;
        break;
      case TCP_KEEPCNT:
        if (p && i->state != S_LISTEN) p->keep_cnt = (u32_t)v;
        break;
      case TCP_MAXSEG:
        break;
      default:
        r = -ENOPROTOOPT;
    }
    net_unlock();
    return r;
  }
  if (level == IPPROTO_IPV6 && s->family == AF_INET6) {
    if (opt == IPV6_JOIN_GROUP || opt == IPV6_LEAVE_GROUP) {
      if (len < 20) return -EINVAL;
      struct {
        u8 addr[16];
        u32 ifindex;
      } mr;
      memcpy(&mr, val, sizeof(mr));
      ip6_addr_t group;
      u32 w[4];
      memcpy(w, mr.addr, 16);
      IP6_ADDR(&group, w[0], w[1], w[2], w[3]);
      net_lock();
      struct netif *nif = mr.ifindex ? netif_get_by_index((u8_t)mr.ifindex) : netif_default;
      err_t e = !nif                     ? ERR_IF
                : opt == IPV6_JOIN_GROUP ? mld6_joingroup_netif(nif, &group)
                                         : mld6_leavegroup_netif(nif, &group);
      net_unlock();
      return lwip_errno(e);
    }
    if (getint(val, len, &v)) return -EINVAL;
    net_lock();
    struct tcp_pcb *pp = i->pcb.any;
    switch (opt) {
      case IPV6_V6ONLY:
        i->v6only = v != 0;
        if (pp && s->type != SOCK_RAW) {
          IP_SET_TYPE_VAL(pp->local_ip, v ? IPADDR_TYPE_V6 : IPADDR_TYPE_ANY);
          IP_SET_TYPE_VAL(pp->remote_ip, v ? IPADDR_TYPE_V6 : IPADDR_TYPE_ANY);
        }
        break;
      case IPV6_UNICAST_HOPS:
        if (pp) pp->ttl = (u8_t)(v < 0 ? 64 : v);
        break;
      case IPV6_MULTICAST_HOPS:
        if (s->type == SOCK_DGRAM) udp_set_multicast_ttl(i->pcb.udp, (u8_t)(v < 0 ? 1 : v));
        break;
      case IPV6_CHECKSUM:
        if (s->type == SOCK_RAW) {
          i->pcb.raw->chksum_reqd = v >= 0;
          i->pcb.raw->chksum_offset = (u16_t)(v >= 0 ? v : 0);
        }
        break;
      case IPV6_TCLASS:
        if (pp) pp->tos = (u8_t)v;
        break;
      case IPV6_MULTICAST_LOOP:
      case IPV6_RECVPKTINFO:
      case IPV6_RECVHOPLIMIT:
        break;
      default:
        r = -ENOPROTOOPT;
    }
    net_unlock();
    return r;
  }
  if (level == IPPROTO_ICMPV6) return 0; /* ICMP6_FILTER: everything is delivered */
  if (level == IPPROTO_IP) {
    if (opt == IP_ADD_MEMBERSHIP || opt == IP_DROP_MEMBERSHIP) {
      if (len < 8) return -EINVAL;
      u32 mr[2];
      memcpy(mr, val, 8);
      ip4_addr_t group, ifaddr;
      ip4_addr_set_u32(&group, mr[0]);
      ip4_addr_set_u32(&ifaddr, mr[1]);
      net_lock();
      err_t e = opt == IP_ADD_MEMBERSHIP ? igmp_joingroup(&ifaddr, &group) : igmp_leavegroup(&ifaddr, &group);
      net_unlock();
      return lwip_errno(e);
    }
    if (getint(val, len, &v)) return -EINVAL;
    net_lock();
    struct ip_pcb *ip = i->pcb.any;
    switch (opt) {
      case IP_TOS:
        if (ip) ((struct tcp_pcb *)ip)->tos = (u8_t)v; /* tos/ttl are common to all pcbs */
        break;
      case IP_TTL:
        if (ip) ((struct tcp_pcb *)ip)->ttl = (u8_t)v;
        break;
      case IP_HDRINCL:
        if (s->type != SOCK_RAW)
          r = -ENOPROTOOPT;
        else
          i->hdrincl = v != 0;
        break;
      case IP_MULTICAST_TTL:
        if (s->type == SOCK_DGRAM) udp_set_multicast_ttl(i->pcb.udp, (u8_t)v);
        break;
      case IP_MULTICAST_LOOP:
      case IP_MULTICAST_IF:
      case IP_PKTINFO:
      case IP_RECVERR:
        break;
      default:
        r = -ENOPROTOOPT;
    }
    net_unlock();
    return r;
  }
  return -ENOPROTOOPT;
}

static int in_getsockopt(struct socket *s, int level, int opt, void *val, u32 *len) {
  struct isock *i = is(s);
  int v;
  if (level == IPPROTO_TCP && s->type == SOCK_STREAM) {
    switch (opt) {
      case TCP_NODELAY:
        v = i->nodelay;
        break;
      case TCP_MAXSEG:
        v = TCP_MSS;
        break;
      case TCP_KEEPIDLE:
        v = i->pcb.tcp && i->state != S_LISTEN ? (int)(i->pcb.tcp->keep_idle / 1000) : 7200;
        break;
      case TCP_KEEPINTVL:
        v = i->pcb.tcp && i->state != S_LISTEN ? (int)(i->pcb.tcp->keep_intvl / 1000) : 75;
        break;
      case TCP_KEEPCNT:
        v = i->pcb.tcp && i->state != S_LISTEN ? (int)i->pcb.tcp->keep_cnt : 9;
        break;
      default:
        return -ENOPROTOOPT;
    }
  } else if (level == IPPROTO_IP) {
    struct tcp_pcb *p = i->pcb.any;
    switch (opt) {
      case IP_TOS:
        v = p ? p->tos : 0;
        break;
      case IP_TTL:
        v = p ? p->ttl : 64;
        break;
      case IP_HDRINCL:
        v = i->hdrincl;
        break;
      default:
        return -ENOPROTOOPT;
    }
  } else {
    return -ENOPROTOOPT;
  }
  *len = MIN(*len, 4u);
  memcpy(val, &v, *len);
  return 0;
}

static unsigned in_poll(struct socket *s, struct file *f, struct poll_table *pt) {
  struct isock *i = is(s);
  poll_wait(f, &s->wq, pt);
  net_lock();
  unsigned m = 0;
  if (s->type == SOCK_STREAM) {
    if (i->state == S_LISTEN) {
      if (!list_empty(&i->acceptq)) m |= POLLIN | POLLRDNORM;
    } else {
      if (i->rx_avail || i->rx_eof || i->shut_rd) m |= POLLIN | POLLRDNORM;
      if (i->rx_eof) m |= POLLRDHUP;
      if (i->err) m |= POLLERR;
      if (i->state == S_CONNECTED && !i->shut_wr && i->pcb.tcp && tcp_sndbuf(i->pcb.tcp) > 0 &&
          tcp_sndqueuelen(i->pcb.tcp) < TCP_SND_QUEUELEN - 4)
        m |= POLLOUT | POLLWRNORM;
      if (i->state == S_CLOSED && (i->err || i->rx_eof)) m |= POLLHUP | POLLOUT;
      if (i->rx_eof && i->shut_wr) m |= POLLHUP;
    }
  } else {
    if (!list_empty(&i->dgq) || i->shut_rd) m |= POLLIN | POLLRDNORM;
    m |= POLLOUT | POLLWRNORM;
  }
  net_unlock();
  return m;
}

static long in_ioctl(struct socket *s, unsigned cmd, u64 arg) {
  struct isock *i = is(s);
  int v = 0;
  switch (cmd) {
    case FIONREAD:
      net_lock();
      if (s->type == SOCK_STREAM)
        v = (int)i->rx_avail;
      else if (!list_empty(&i->dgq))
        v = list_first_entry(&i->dgq, struct dgram, link)->p->tot_len;
      net_unlock();
      return put_user(v, (int *)arg);
    case TIOCOUTQ:
      net_lock();
      if (s->type == SOCK_STREAM && i->pcb.tcp && i->state == S_CONNECTED) v = TCP_SND_BUF - tcp_sndbuf(i->pcb.tcp);
      net_unlock();
      return put_user(v, (int *)arg);
    default:
      return netdev_ioctl(cmd, arg);
  }
}

static const struct proto_ops inet_ops = {
    .release = in_release,
    .bind = in_bind,
    .connect = in_connect,
    .listen = in_listen,
    .accept = in_accept,
    .sendmsg = in_sendmsg,
    .recvmsg = in_recvmsg,
    .shutdown = in_shutdown,
    .getname = in_getname,
    .setsockopt = in_setsockopt,
    .getsockopt = in_getsockopt,
    .poll = in_poll,
    .ioctl = in_ioctl,
};

static int in_create(struct socket *s, int type, int protocol) {
  if (type == SOCK_RAW && current->proc->cred.euid != 0) return -EPERM;
  if (type == SOCK_STREAM && protocol && protocol != IPPROTO_TCP) return -EPROTONOSUPPORT;
  if (type == SOCK_DGRAM && protocol && protocol != IPPROTO_UDP) return -EPROTONOSUPPORT;
  if (type == SOCK_RAW && (protocol <= 0 || protocol > 255)) return -EPROTONOSUPPORT;
  if (type == SOCK_STREAM && s->family == AF_INET6 && protocol == 0) protocol = IPPROTO_TCP;
  if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_RAW) return -ESOCKTNOSUPPORT;
  struct isock *i = isock_new(s);
  if (!i) return -ENOMEM;
  s->ops = &inet_ops;
  s->protocol = type == SOCK_STREAM ? IPPROTO_TCP : type == SOCK_DGRAM ? IPPROTO_UDP : protocol;
  u8_t iptype = s->family == AF_INET ? IPADDR_TYPE_V4 : IPADDR_TYPE_ANY; /* AF_INET6 is dual-stack */
  net_lock();
  switch (type) {
    case SOCK_STREAM:
      i->pcb.tcp = tcp_new_ip_type(iptype);
      if (i->pcb.tcp) tcp_setup(i, i->pcb.tcp);
      break;
    case SOCK_DGRAM:
      i->pcb.udp = udp_new_ip_type(iptype);
      if (i->pcb.udp) udp_recv(i->pcb.udp, udp_recv_cb, i);
      break;
    case SOCK_RAW:
      i->pcb.raw = raw_new_ip_type(s->family == AF_INET ? IPADDR_TYPE_V4 : IPADDR_TYPE_V6, (u8_t)protocol);
      if (i->pcb.raw) {
        raw_recv(i->pcb.raw, raw_recv_cb, i);
        if (s->family == AF_INET6 && protocol == IPPROTO_ICMPV6) { /* the kernel computes ICMPv6 checksums */
          i->pcb.raw->chksum_reqd = 1;
          i->pcb.raw->chksum_offset = 2;
        }
      }
      break;
  }
  net_unlock();
  if (!i->pcb.any) {
    kfree(i);
    s->priv = NULL;
    return -ENOBUFS;
  }
  return 0;
}

/* ---------------- /proc/net/{tcp,udp} ---------------- */

static int linux_tcp_state(enum tcp_state st) {
  static const int map[] = {
      [CLOSED] = 7,     [LISTEN] = 10,    [SYN_SENT] = 2, [SYN_RCVD] = 3, [ESTABLISHED] = 1, [FIN_WAIT_1] = 4,
      [FIN_WAIT_2] = 5, [CLOSE_WAIT] = 8, [CLOSING] = 11, [LAST_ACK] = 9, [TIME_WAIT] = 6};
  return map[st];
}

static void show_pcb(seq_printf_t pr, void *ctx, int *n, u32 la, u16 lp, u32 ra, u16 rp, int st) {
  pr(ctx, "%4d: %08X:%04X %08X:%04X %02X 00000000:00000000 00:00000000 00000000     0        0 0 1 0000000000000000\n",
     (*n)++, la, lp, ra, rp, st);
}

static void hex6(char *out, const ip_addr_t *a) {
  u32 w[4] = {0, 0, 0, 0};
  if (IP_IS_V6(a))
    memcpy(w, ip_2_ip6(a)->addr, 16);
  else if (IP_IS_V4(a) && !ip_addr_isany(a))
    w[2] = lwip_htonl(0xffff), w[3] = ip4_addr_get_u32(ip_2_ip4(a));
  snprintf(out, 33, "%08X%08X%08X%08X", w[0], w[1], w[2], w[3]);
}

static void show_pcb6(seq_printf_t pr, void *ctx, int *n, const ip_addr_t *la, u16 lp, const ip_addr_t *ra, u16 rp,
                      int st) {
  char l[33], r[33];
  hex6(l, la);
  hex6(r, ra);
  pr(ctx, "%4d: %s:%04X %s:%04X %02X 00000000:00000000 00:00000000 00000000     0        0 0 1 0000000000000000\n",
     (*n)++, l, lp, r, rp, st);
}

static bool is6(const ip_addr_t *local) { return !IP_IS_V4(local); }

static void show_tcp_family(seq_printf_t pr, void *ctx, bool v6) {
  pr(ctx,
     v6 ? "  sl  local_address                         remote_address                        st tx_queue rx_queue tr "
          "tm->when retrnsmt   uid  timeout inode\n"
        : "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n");
  int n = 0;
  net_lock();
  for (struct tcp_pcb_listen *l = tcp_listen_pcbs.listen_pcbs; l; l = l->next) {
    if (is6(&l->local_ip) != v6) continue;
    if (v6)
      show_pcb6(pr, ctx, &n, &l->local_ip, l->local_port, IP6_ADDR_ANY, 0, 10);
    else
      show_pcb(pr, ctx, &n, ip4_addr_get_u32(ip_2_ip4(&l->local_ip)), l->local_port, 0, 0, 10);
  }
  struct tcp_pcb *lists[] = {tcp_active_pcbs, tcp_tw_pcbs};
  for (int k = 0; k < 2; k++)
    for (struct tcp_pcb *p = lists[k]; p; p = p->next) {
      if (is6(&p->local_ip) != v6) continue;
      if (v6)
        show_pcb6(pr, ctx, &n, &p->local_ip, p->local_port, &p->remote_ip, p->remote_port, linux_tcp_state(p->state));
      else
        show_pcb(pr, ctx, &n, ip4_addr_get_u32(ip_2_ip4(&p->local_ip)), p->local_port,
                 ip4_addr_get_u32(ip_2_ip4(&p->remote_ip)), p->remote_port, linux_tcp_state(p->state));
    }
  net_unlock();
}

static void show_tcp(seq_printf_t pr, void *ctx) { show_tcp_family(pr, ctx, false); }
static void show_tcp6(seq_printf_t pr, void *ctx) { show_tcp_family(pr, ctx, true); }

static void show_udp_family(seq_printf_t pr, void *ctx, bool v6) {
  pr(ctx, "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n");
  int n = 0;
  net_lock();
  for (struct udp_pcb *p = udp_pcbs; p; p = p->next) {
    if (is6(&p->local_ip) != v6) continue;
    if (v6)
      show_pcb6(pr, ctx, &n, &p->local_ip, p->local_port, &p->remote_ip, p->remote_port, 7);
    else
      show_pcb(pr, ctx, &n, ip4_addr_get_u32(ip_2_ip4(&p->local_ip)), p->local_port,
               ip4_addr_get_u32(ip_2_ip4(&p->remote_ip)), p->remote_port, 7);
  }
  net_unlock();
}

static void show_udp(seq_printf_t pr, void *ctx) { show_udp_family(pr, ctx, false); }
static void show_udp6(seq_printf_t pr, void *ctx) { show_udp_family(pr, ctx, true); }

/* raw sockets are not enumerable through lwIP's API: the tables list none */
static void show_raw_family(seq_printf_t pr, void *ctx, bool v6) {
  pr(ctx, "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n");
}

static void show_raw(seq_printf_t pr, void *ctx) { show_raw_family(pr, ctx, false); }
static void show_raw6(seq_printf_t pr, void *ctx) { show_raw_family(pr, ctx, true); }

static const struct net_family inet_family = {.family = AF_INET, .create = in_create};
static const struct net_family inet6_family = {.family = AF_INET6, .create = in_create};

static int inet_init(void) {
  net_register_family(&inet_family);
  net_register_family(&inet6_family);
  proc_net_register("tcp", show_tcp);
  proc_net_register("udp", show_udp);
  proc_net_register("tcp6", show_tcp6);
  proc_net_register("udp6", show_udp6);
  proc_net_register("raw", show_raw);
  proc_net_register("raw6", show_raw6);
  return 0;
}
core_initcall(inet_init);
