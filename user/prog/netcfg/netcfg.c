/*
 * netcfg - configure network interfaces.
 *
 *   netcfg                                  show interfaces
 *   netcfg IFACE dhcp [-t SECONDS] [-b]      lease an address (the kernel's DHCP
 *                                           client renews it); -b: wait in the
 *                                           background
 *   netcfg IFACE static ADDR/PREFIX [GATEWAY [DNS...]]
 *   netcfg IFACE down
 *
 * On success /etc/resolv.conf lists the DNS servers.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SIOCOLUX_DHCP 0x89f0
#define SIOCOLUX_DNS 0x89f1

static int sock;

static int ifr_ioctl(const char *ifname, unsigned long cmd, struct ifreq *r) {
  size_t n = strnlen(ifname, IFNAMSIZ - 1);
  memcpy(r->ifr_name, ifname, n);
  r->ifr_name[n] = 0;
  return ioctl(sock, cmd, r);
}

static int dhcp_ctl(const char *ifname, int op) {
  struct ifreq r;
  memset(&r, 0, sizeof(r));
  r.ifr_ifindex = op;
  if (ifr_ioctl(ifname, SIOCOLUX_DHCP, &r) < 0) return -1;
  return r.ifr_ifindex;
}

static in_addr_t if_addr(const char *ifname, unsigned long cmd) {
  struct ifreq r;
  memset(&r, 0, sizeof(r));
  if (ifr_ioctl(ifname, cmd, &r) < 0) return 0;
  return ((struct sockaddr_in *)&r.ifr_addr)->sin_addr.s_addr;
}

static int prefix_of(in_addr_t mask) { return __builtin_popcount(ntohl(mask)); }

static void write_resolv(const in_addr_t *servers, int n) {
  if (n == 0) return;
  FILE *f = fopen("/etc/resolv.conf.new", "w");
  if (!f) return;
  for (int i = 0; i < n; i++)
    if (servers[i]) fprintf(f, "nameserver %s\n", inet_ntoa((struct in_addr){servers[i]}));
  fclose(f);
  rename("/etc/resolv.conf.new", "/etc/resolv.conf");
}

static void show(void) {
  FILE *f = fopen("/proc/net/dev", "r");
  if (!f) return;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    char *colon = strchr(line, ':');
    if (!colon) continue;
    *colon = 0;
    char *name = line;
    while (*name == ' ') name++;
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    ifr_ioctl(name, SIOCGIFFLAGS, &r);
    in_addr_t a = if_addr(name, SIOCGIFADDR), m = if_addr(name, SIOCGIFNETMASK);
    printf("%-6s %-4s %s/%d%s\n", name, (r.ifr_flags & IFF_UP) ? "up" : "down", inet_ntoa((struct in_addr){a}),
           prefix_of(m), strncmp(name, "lo", 2) && dhcp_ctl(name, 2) == 1 ? " (dhcp)" : "");
  }
  fclose(f);
}

static int do_dhcp(const char *ifname, int timeout, int background) {
  if (dhcp_ctl(ifname, 1) < 0) {
    fprintf(stderr, "netcfg: %s: %s\n", ifname, strerror(errno));
    return 1;
  }
  if (background) {
    pid_t p = fork();
    if (p > 0) return 0;
    if (p == 0) setsid();
  }
  time_t end = time(NULL) + timeout;
  while (dhcp_ctl(ifname, 2) != 1) {
    if (time(NULL) >= end) {
      fprintf(stderr, "netcfg: %s: no DHCP lease after %d s (still trying)\n", ifname, timeout);
      return 1;
    }
    usleep(100000);
  }
  in_addr_t dns[3] = {0};
  ioctl(sock, SIOCOLUX_DNS, dns);
  write_resolv(dns, 3);
  in_addr_t a = if_addr(ifname, SIOCGIFADDR), m = if_addr(ifname, SIOCGIFNETMASK);
  char addr[16];
  strcpy(addr, inet_ntoa((struct in_addr){a}));
  printf("%s: %s/%d (dhcp), dns %s\n", ifname, addr, prefix_of(m), dns[0] ? inet_ntoa((struct in_addr){dns[0]}) : "-");
  return 0;
}

static int set_addr(const char *ifname, unsigned long cmd, in_addr_t a) {
  struct ifreq r;
  memset(&r, 0, sizeof(r));
  struct sockaddr_in *sin = (struct sockaddr_in *)&r.ifr_addr;
  sin->sin_family = AF_INET;
  sin->sin_addr.s_addr = a;
  return ifr_ioctl(ifname, cmd, &r);
}

static int do_static(const char *ifname, int argc, char **argv) {
  char *slash = strchr(argv[0], '/');
  int prefix = 24;
  if (slash) {
    *slash = 0;
    prefix = atoi(slash + 1);
  }
  struct in_addr a;
  if (!inet_aton(argv[0], &a) || prefix < 0 || prefix > 32) {
    fprintf(stderr, "netcfg: bad address '%s'\n", argv[0]);
    return 2;
  }
  in_addr_t mask = prefix ? htonl(~0u << (32 - prefix)) : 0;
  dhcp_ctl(ifname, 0);
  if (set_addr(ifname, SIOCSIFADDR, a.s_addr) || set_addr(ifname, SIOCSIFNETMASK, mask)) {
    fprintf(stderr, "netcfg: %s: %s\n", ifname, strerror(errno));
    return 1;
  }
  struct ifreq r;
  memset(&r, 0, sizeof(r));
  r.ifr_flags = IFF_UP;
  ifr_ioctl(ifname, SIOCSIFFLAGS, &r);
  if (argc > 1) {
    struct rtentry rt;
    memset(&rt, 0, sizeof(rt));
    struct sockaddr_in *gw = (struct sockaddr_in *)&rt.rt_gateway;
    gw->sin_family = AF_INET;
    if (!inet_aton(argv[1], &gw->sin_addr)) return 2;
    ((struct sockaddr_in *)&rt.rt_dst)->sin_family = AF_INET;
    ((struct sockaddr_in *)&rt.rt_genmask)->sin_family = AF_INET;
    rt.rt_flags = RTF_UP | RTF_GATEWAY;
    rt.rt_dev = (char *)ifname;
    if (ioctl(sock, SIOCADDRT, &rt) < 0) {
      fprintf(stderr, "netcfg: default route via %s: %s\n", argv[1], strerror(errno));
      return 1;
    }
  }
  in_addr_t dns[3] = {0};
  int n = 0;
  for (int i = 2; i < argc && n < 3; i++) {
    struct in_addr d;
    if (inet_aton(argv[i], &d)) dns[n++] = d.s_addr;
  }
  write_resolv(dns, n);
  return 0;
}

int main(int argc, char **argv) {
  sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (sock < 0) {
    perror("netcfg: socket");
    return 1;
  }
  if (argc == 1) {
    show();
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[2], "dhcp")) {
    int timeout = 30, bg = 0;
    for (int i = 3; i < argc; i++) {
      if (!strcmp(argv[i], "-t") && i + 1 < argc)
        timeout = atoi(argv[++i]);
      else if (!strcmp(argv[i], "-b"))
        bg = 1;
    }
    return do_dhcp(argv[1], timeout, bg);
  }
  if (argc >= 4 && !strcmp(argv[2], "static")) return do_static(argv[1], argc - 3, argv + 3);
  if (argc == 3 && !strcmp(argv[2], "down")) {
    dhcp_ctl(argv[1], 0);
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    return ifr_ioctl(argv[1], SIOCSIFFLAGS, &r) ? 1 : 0;
  }
  fprintf(stderr,
          "usage: netcfg [IFACE dhcp [-t SECONDS] [-b] | IFACE static ADDR/PREFIX [GATEWAY [DNS...]] | IFACE down]\n");
  return 2;
}
