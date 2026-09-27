/* lwIP configuration for the OluxOS kernel (see third_party/lwip/README.oluxos). */
#ifndef OLUX_LWIPOPTS_H
#define OLUX_LWIPOPTS_H

/* Bare-metal mode: the kernel serialises every call with net_lock() and
 * drives timers and input from the "netd" kernel thread. */
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_NETIF_API 0

/* Memory comes from the kernel heap. */
#define MEM_LIBC_MALLOC 1
#define MEMP_MEM_MALLOC 1
#define MEM_ALIGNMENT 8
#define mem_clib_malloc lwip_kmalloc
#define mem_clib_free lwip_kfree
#define mem_clib_calloc lwip_kcalloc

/* Protocols */
#define LWIP_IPV4 1
#define LWIP_IPV6 1
#define LWIP_IPV6_AUTOCONFIG 1 /* SLAAC from router advertisements */
#define LWIP_IPV6_NUM_ADDRESSES 4
#define LWIP_IPV6_MLD 1
#define LWIP_IPV6_FRAG 1
#define LWIP_IPV6_REASS 1
#define IPV6_FRAG_COPYHEADER 1 /* 64-bit pointers do not fit the fragment header */
#define LWIP_ND6_RDNSS_MAX_DNS_SERVERS 1
#define LWIP_IPV6_DHCP6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define ETHARP_SUPPORT_STATIC_ENTRIES 1
#define LWIP_ICMP 1
#define LWIP_BROADCAST_PING 1
#define LWIP_MULTICAST_PING 1
#define LWIP_RAW 1
#define LWIP_UDP 1
#define LWIP_UDPLITE 0
#define LWIP_TCP 1
#define LWIP_IGMP 1
#define LWIP_DHCP 1
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_DNS 1 /* only to keep the DNS servers DHCP reports */
#define DNS_MAX_SERVERS 3 /* DHCP servers first, then RDNSS */
#define IP_FORWARD 0
#define IP_REASSEMBLY 1
#define IP_FRAG 1
#define SO_REUSE 1
#define SO_REUSE_RXTOALL 1

/* TCP: 64 KiB windows, keepalive */
#define TCP_MSS 1460
#define TCP_WND (44 * TCP_MSS)
#define TCP_SND_BUF (44 * TCP_MSS)
#define TCP_SND_QUEUELEN (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_OVERSIZE TCP_MSS
#define LWIP_TCP_KEEPALIVE 1
#define TCP_LISTEN_BACKLOG 1
#define TCP_DEFAULT_LISTEN_BACKLOG 128
#define LWIP_TCP_TIMESTAMPS 0
#define TCP_QUEUE_OOSEQ 1
#define MEMP_NUM_TCP_SEG 4096

/* Interfaces */
#define LWIP_NETIF_LOOPBACK 1
#define LWIP_HAVE_LOOPIF 1
#define LWIP_LOOPBACK_MAX_PBUFS 0
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1
#define LWIP_SINGLE_NETIF 0
#define LWIP_NUM_NETIF_CLIENT_DATA 1

#define PBUF_LINK_HLEN 14
#define PBUF_POOL_BUFSIZE 1536
#define ETH_PAD_SIZE 0
#define LWIP_CHECKSUM_ON_COPY 1

#define LWIP_STATS 0
#define LWIP_STATS_DISPLAY 0

/* Randomness and diagnostics from the kernel */
#define LWIP_RAND() lwip_rand()

#ifndef __ASSEMBLY__
#include <stddef.h>
void *lwip_kmalloc(size_t n);
void *lwip_kcalloc(size_t n, size_t m);
void lwip_kfree(void *p);
unsigned int lwip_rand(void);
#endif

#endif
