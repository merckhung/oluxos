/* BSD socket layer: generic part shared by the protocol families. */
#ifndef OLUX_NET_H
#define OLUX_NET_H

#include <olux/fs.h>
#include <olux/types.h>
#include <olux/wait.h>

#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10
#define AF_NETLINK 16
#define AF_PACKET 17
#define AF_MAX 32

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_SEQPACKET 5
#define SOCK_TYPE_MASK 0xf
#define SOCK_NONBLOCK 04000
#define SOCK_CLOEXEC 02000000

#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_TYPE 3
#define SO_ERROR 4
#define SO_DONTROUTE 5
#define SO_BROADCAST 6
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_OOBINLINE 10
#define SO_LINGER 13
#define SO_REUSEPORT 15
#define SO_PASSCRED 16
#define SO_PEERCRED 17
#define SO_RCVLOWAT 18
#define SO_SNDLOWAT 19
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#define SO_BINDTODEVICE 25
#define SO_TIMESTAMP 29
#define SO_ACCEPTCONN 30
#define SO_PROTOCOL 38
#define SO_DOMAIN 39

#define SCM_RIGHTS 1
#define SCM_CREDENTIALS 2

#define MSG_OOB 0x1
#define MSG_PEEK 0x2
#define MSG_DONTROUTE 0x4
#define MSG_CTRUNC 0x8
#define MSG_TRUNC 0x20
#define MSG_DONTWAIT 0x40
#define MSG_EOR 0x80
#define MSG_WAITALL 0x100
#define MSG_NOSIGNAL 0x4000
#define MSG_CMSG_CLOEXEC 0x40000000

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#define SOCKADDR_MAX 128
#define MAX_IOV 1024
#define SCM_MAX_FD 253

struct ucred {
  s32 pid;
  u32 uid, gid;
};

/* A message in kernel form: user or kernel buffers, optional address and
 * control data (already copied into the kernel). */
struct kmsg {
  struct iobuf *iov;
  int iovcnt;
  size_t len;            /* total bytes described by iov */
  u8 name[SOCKADDR_MAX]; /* address (sendmsg: destination, recvmsg: source) */
  u32 namelen;
  struct file **fds; /* SCM_RIGHTS: files sent / received */
  int nfds;
  bool want_creds;    /* recvmsg: caller has SO_PASSCRED */
  struct ucred creds; /* recvmsg: sender credentials, if have_creds */
  bool have_creds;
  u32 flags; /* recvmsg: MSG_TRUNC / MSG_CTRUNC out */
};

/* Copy between a kmsg's iov and kernel memory, starting at byte `off`. */
int kmsg_copy_to(struct kmsg *m, size_t off, const void *src, size_t n);
int kmsg_copy_from(struct kmsg *m, size_t off, void *dst, size_t n);

struct socket;
struct poll_table;

struct proto_ops {
  int (*release)(struct socket *s);
  int (*bind)(struct socket *s, const void *addr, u32 len);
  int (*connect)(struct socket *s, const void *addr, u32 len);
  int (*listen)(struct socket *s, int backlog);
  /* Returns a new, connected socket (not yet attached to a file). */
  int (*accept)(struct socket *s, struct socket **out);
  ssize_t (*sendmsg)(struct socket *s, struct kmsg *m, int flags);
  ssize_t (*recvmsg)(struct socket *s, struct kmsg *m, int flags);
  int (*shutdown)(struct socket *s, int how);
  int (*getname)(struct socket *s, void *addr, u32 *len, bool peer);
  int (*setsockopt)(struct socket *s, int level, int opt, const void *val, u32 len);
  int (*getsockopt)(struct socket *s, int level, int opt, void *val, u32 *len);
  unsigned (*poll)(struct socket *s, struct file *f, struct poll_table *pt);
  long (*ioctl)(struct socket *s, unsigned cmd, u64 arg);
};

struct socket {
  int family, type, protocol;
  const struct proto_ops *ops;
  void *priv;
  struct file *file;    /* NULL until attached */
  struct wait_queue wq; /* readers, writers, connect/accept waiters */
  int so_error;
  bool reuseaddr, keepalive, broadcast, passcred, listening;
  s64 rcvtimeo_ns, sndtimeo_ns; /* 0 = forever */
  u32 sndbuf, rcvbuf;
  struct ucred owner; /* creator's credentials (SO_PEERCRED) */
};

struct net_family {
  int family;
  int (*create)(struct socket *s, int type, int protocol);
};
void net_register_family(const struct net_family *f);

struct socket *sock_alloc(int family, int type, int protocol);
void sock_free(struct socket *s);
int sock_attach_file(struct socket *s, int flags); /* returns fd */
bool sock_nonblock(struct socket *s, int msg_flags);
/* Wait for `cond` honouring non-blocking mode, the timeout and signals. */
#define sock_wait(s, cond, nonblock, timeout_ns)                                                               \
  ({                                                                                                           \
    int __sr = 0;                                                                                              \
    if (!(cond)) {                                                                                             \
      if (nonblock) {                                                                                          \
        __sr = -EAGAIN;                                                                                        \
      } else {                                                                                                 \
        long __st = wait_event_interruptible_timeout((s)->wq, (cond), (timeout_ns) ? (long)(timeout_ns) : -1); \
        if (__st == -ERESTARTSYS)                                                                              \
          __sr = -ERESTARTSYS;                                                                                 \
        else if (__st == 0)                                                                                    \
          __sr = -EAGAIN;                                                                                      \
      }                                                                                                        \
    }                                                                                                          \
    __sr;                                                                                                      \
  })
struct socket *sock_from_file(struct file *f);
bool is_socket_file(struct file *f);

#endif
