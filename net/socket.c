/*
 * Generic BSD socket layer: socket files, the socket system calls, msghdr
 * and control-message (SCM_RIGHTS, SCM_CREDENTIALS) handling and the
 * protocol-independent socket options. Protocol families (net/unix.c,
 * net/inet.c) implement struct proto_ops.
 */
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/net.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/signal.h>
#include <olux/time.h>
#include <olux/uaccess.h>

static const struct net_family *families[AF_MAX];

void net_register_family(const struct net_family *f) {
  if (f->family > 0 && f->family < AF_MAX) families[f->family] = f;
}

struct inode *anon_inode(mode_t mode);

int kmsg_copy_to(struct kmsg *m, size_t off, const void *src, size_t n) {
  const u8 *s = src;
  for (int i = 0; i < m->iovcnt && n; i++) {
    struct iobuf *b = &m->iov[i];
    if (off >= b->len) {
      off -= b->len;
      continue;
    }
    size_t c = MIN(n, b->len - off);
    if (iob_write(b, off, s, c)) return -EFAULT;
    s += c;
    n -= c;
    off = 0;
  }
  return 0;
}

int kmsg_copy_from(struct kmsg *m, size_t off, void *dst, size_t n) {
  u8 *d = dst;
  for (int i = 0; i < m->iovcnt && n; i++) {
    struct iobuf *b = &m->iov[i];
    if (off >= b->len) {
      off -= b->len;
      continue;
    }
    size_t c = MIN(n, b->len - off);
    if (iob_read(b, off, d, c)) return -EFAULT;
    d += c;
    n -= c;
    off = 0;
  }
  return 0;
}

/* ---------------- socket objects and files ---------------- */

struct socket *sock_alloc(int family, int type, int protocol) {
  struct socket *s = kzalloc(sizeof(*s), 0);
  if (!s) return NULL;
  s->family = family;
  s->type = type;
  s->protocol = protocol;
  wq_init(&s->wq);
  s->sndbuf = s->rcvbuf = 212992;
  struct process *p = current->proc;
  if (p) s->owner = (struct ucred){p->pid, p->cred.euid, p->cred.egid};
  return s;
}

void sock_free(struct socket *s) {
  if (s->ops && s->ops->release) s->ops->release(s);
  kfree(s);
}

static const struct file_operations socket_fops;

bool is_socket_file(struct file *f) { return f && f->f_op == &socket_fops; }

struct socket *sock_from_file(struct file *f) { return is_socket_file(f) ? f->priv : NULL; }

bool sock_nonblock(struct socket *s, int msg_flags) {
  return (msg_flags & MSG_DONTWAIT) || (s->file && (s->file->flags & O_NONBLOCK));
}

static int sock_release_file(struct inode *i, struct file *f) {
  struct socket *s = f->priv;
  if (s) sock_free(s);
  return 0;
}

static ssize_t sock_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct socket *s = f->priv;
  struct kmsg m = {.iov = b, .iovcnt = 1, .len = b->len};
  return s->ops->recvmsg ? s->ops->recvmsg(s, &m, 0) : -EOPNOTSUPP;
}

static void sigpipe(int flags) {
  if (!(flags & MSG_NOSIGNAL)) send_signal_thread(current, SIGPIPE, NULL);
}

static ssize_t sock_write(struct file *f, struct iobuf *b, loff_t *pos) {
  struct socket *s = f->priv;
  struct kmsg m = {.iov = b, .iovcnt = 1, .len = b->len};
  ssize_t r = s->ops->sendmsg ? s->ops->sendmsg(s, &m, 0) : -EOPNOTSUPP;
  if (r == -EPIPE) sigpipe(0);
  return r;
}

static unsigned sock_poll(struct file *f, struct poll_table *pt) {
  struct socket *s = f->priv;
  return s->ops->poll ? s->ops->poll(s, f, pt) : 0;
}

static long sock_ioctl(struct file *f, unsigned cmd, u64 arg) {
  struct socket *s = f->priv;
  return s->ops->ioctl ? s->ops->ioctl(s, cmd, arg) : -ENOTTY;
}

static const struct file_operations socket_fops = {
    .release = sock_release_file, .read = sock_read, .write = sock_write, .poll = sock_poll, .ioctl = sock_ioctl};

int sock_attach_file(struct socket *s, int flags) {
  struct file *f = file_alloc();
  if (!f) return -ENOMEM;
  f->inode = anon_inode(S_IFSOCK | 0777);
  f->f_op = &socket_fops;
  f->priv = s;
  f->mode = FMODE_READ | FMODE_WRITE;
  f->flags = O_RDWR | (flags & SOCK_NONBLOCK ? O_NONBLOCK : 0);
  s->file = f;
  int fd = fd_install(f, 0, flags & SOCK_CLOEXEC);
  if (fd < 0) {
    f->priv = NULL; /* the caller still owns the socket */
    s->file = NULL;
    file_put(f);
  }
  return fd;
}

static struct socket *sockfd(int fd, struct file **fp, int *err) {
  struct file *f = fget(fd);
  if (!f) {
    *err = -EBADF;
    return NULL;
  }
  if (!is_socket_file(f)) {
    file_put(f);
    *err = -ENOTSOCK;
    return NULL;
  }
  *fp = f;
  return f->priv;
}

static int create(int family, int type, int protocol, struct socket **out) {
  if (family <= 0 || family >= AF_MAX || !families[family]) return -EAFNOSUPPORT;
  struct socket *s = sock_alloc(family, type & SOCK_TYPE_MASK, protocol);
  if (!s) return -ENOMEM;
  int r = families[family]->create(s, type & SOCK_TYPE_MASK, protocol);
  if (r) {
    kfree(s);
    return r;
  }
  *out = s;
  return 0;
}

/* ---------------- system calls ---------------- */

long sys_socket(u64 family, u64 type, u64 protocol);
long sys_socket(u64 family, u64 type, u64 protocol) {
  if (type & ~(u64)(SOCK_TYPE_MASK | SOCK_NONBLOCK | SOCK_CLOEXEC)) return -EINVAL;
  struct socket *s;
  int r = create((int)family, (int)type, (int)protocol, &s);
  if (r) return r;
  int fd = sock_attach_file(s, (int)type);
  if (fd < 0) sock_free(s);
  return fd;
}

int unix_socketpair(struct socket *a, struct socket *b);

long sys_socketpair(u64 family, u64 type, u64 protocol, u64 usv);
long sys_socketpair(u64 family, u64 type, u64 protocol, u64 usv) {
  if (family != AF_UNIX) return -EOPNOTSUPP;
  struct socket *a, *b;
  int r = create((int)family, (int)type, (int)protocol, &a);
  if (r) return r;
  r = create((int)family, (int)type, (int)protocol, &b);
  if (r) {
    sock_free(a);
    return r;
  }
  r = unix_socketpair(a, b);
  if (r) {
    sock_free(a);
    sock_free(b);
    return r;
  }
  int fda = sock_attach_file(a, (int)type);
  if (fda < 0) {
    sock_free(a);
    sock_free(b);
    return fda;
  }
  int fdb = sock_attach_file(b, (int)type);
  if (fdb < 0) {
    sock_free(b);
    fd_close(fda);
    return fdb;
  }
  int sv[2] = {fda, fdb};
  if (copy_to_user(usv, sv, sizeof(sv))) return -EFAULT;
  return 0;
}

static int get_addr(u64 uaddr, u64 len, u8 *buf) {
  if (len > SOCKADDR_MAX) return -EINVAL;
  if (len && copy_from_user(buf, uaddr, len)) return -EFAULT;
  return 0;
}

/* Copy an address out, truncating as POSIX requires; *ulen gets the full length. */
static int put_addr(const u8 *addr, u32 len, u64 uaddr, u64 ulen) {
  if (!uaddr || !ulen) return 0;
  u32 have;
  if (copy_from_user(&have, ulen, 4)) return -EFAULT;
  if ((s32)have < 0) return -EINVAL;
  if (copy_to_user(uaddr, addr, MIN(have, len))) return -EFAULT;
  return copy_to_user(ulen, &len, 4);
}

long sys_bind(u64 fd, u64 uaddr, u64 len);
long sys_bind(u64 fd, u64 uaddr, u64 len) {
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  u8 addr[SOCKADDR_MAX];
  err = get_addr(uaddr, len, addr);
  if (!err) err = s->ops->bind ? s->ops->bind(s, addr, (u32)len) : -EOPNOTSUPP;
  file_put(f);
  return err;
}

long sys_listen(u64 fd, u64 backlog);
long sys_listen(u64 fd, u64 backlog) {
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  int bl = (int)backlog;
  if (bl <= 0) bl = 1;
  if (bl > 4096) bl = 4096;
  err = s->ops->listen ? s->ops->listen(s, bl) : -EOPNOTSUPP;
  if (!err) s->listening = true;
  file_put(f);
  return err;
}

long sys_accept4(u64 fd, u64 uaddr, u64 ulen, u64 flags);
long sys_accept4(u64 fd, u64 uaddr, u64 ulen, u64 flags) {
  if (flags & ~(u64)(SOCK_NONBLOCK | SOCK_CLOEXEC)) return -EINVAL;
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  struct socket *ns = NULL;
  err = s->ops->accept ? s->ops->accept(s, &ns) : -EOPNOTSUPP;
  file_put(f);
  if (err) return err;
  u8 addr[SOCKADDR_MAX];
  u32 alen = sizeof(addr);
  if (uaddr && ns->ops->getname && !ns->ops->getname(ns, addr, &alen, true)) {
    err = put_addr(addr, alen, uaddr, ulen);
    if (err) {
      sock_free(ns);
      return err;
    }
  }
  int nfd = sock_attach_file(ns, (int)flags);
  if (nfd < 0) sock_free(ns);
  return nfd;
}

long sys_accept(u64 fd, u64 uaddr, u64 ulen);
long sys_accept(u64 fd, u64 uaddr, u64 ulen) { return sys_accept4(fd, uaddr, ulen, 0); }

long sys_connect(u64 fd, u64 uaddr, u64 len);
long sys_connect(u64 fd, u64 uaddr, u64 len) {
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  u8 addr[SOCKADDR_MAX];
  err = get_addr(uaddr, len, addr);
  if (!err) err = s->ops->connect ? s->ops->connect(s, addr, (u32)len) : -EOPNOTSUPP;
  file_put(f);
  return err;
}

static long getname(u64 fd, u64 uaddr, u64 ulen, bool peer) {
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  u8 addr[SOCKADDR_MAX];
  u32 alen = sizeof(addr);
  err = s->ops->getname ? s->ops->getname(s, addr, &alen, peer) : -EOPNOTSUPP;
  if (!err) err = put_addr(addr, alen, uaddr, ulen);
  file_put(f);
  return err;
}

long sys_getsockname(u64 fd, u64 uaddr, u64 ulen);
long sys_getsockname(u64 fd, u64 uaddr, u64 ulen) { return getname(fd, uaddr, ulen, false); }
long sys_getpeername(u64 fd, u64 uaddr, u64 ulen);
long sys_getpeername(u64 fd, u64 uaddr, u64 ulen) { return getname(fd, uaddr, ulen, true); }

static void drop_fds(struct kmsg *m) {
  for (int i = 0; i < m->nfds; i++)
    if (m->fds[i]) file_put(m->fds[i]);
  kfree(m->fds);
  m->fds = NULL;
  m->nfds = 0;
}

static ssize_t do_send(struct socket *s, struct kmsg *m, int flags) {
  if (!s->ops->sendmsg) return -EOPNOTSUPP;
  ssize_t r = s->ops->sendmsg(s, m, flags);
  if (r == -EPIPE && (s->type == SOCK_STREAM || s->type == SOCK_SEQPACKET)) sigpipe(flags);
  drop_fds(m); /* whatever the protocol did not take */
  return r;
}

long sys_sendto(u64 fd, u64 buf, u64 len, u64 flags, u64 uaddr, u64 alen);
long sys_sendto(u64 fd, u64 buf, u64 len, u64 flags, u64 uaddr, u64 alen) {
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  struct iobuf b = ubuf(buf, len);
  struct kmsg m = {.iov = &b, .iovcnt = 1, .len = len};
  ssize_t r = 0;
  if (!access_ok(buf, len)) r = -EFAULT;
  if (!r && uaddr) {
    r = get_addr(uaddr, alen, m.name);
    m.namelen = (u32)alen;
  }
  if (!r) r = do_send(s, &m, (int)flags);
  file_put(f);
  return r;
}

long sys_recvfrom(u64 fd, u64 buf, u64 len, u64 flags, u64 uaddr, u64 ulen);
long sys_recvfrom(u64 fd, u64 buf, u64 len, u64 flags, u64 uaddr, u64 ulen) {
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  struct iobuf b = ubuf(buf, len);
  struct kmsg m = {.iov = &b, .iovcnt = 1, .len = len};
  ssize_t r = access_ok(buf, len) ? 0 : -EFAULT;
  if (!r) r = s->ops->recvmsg ? s->ops->recvmsg(s, &m, (int)flags) : -EOPNOTSUPP;
  drop_fds(&m);
  if (r >= 0 && uaddr && m.namelen) {
    int e = put_addr(m.name, m.namelen, uaddr, ulen);
    if (e) r = e;
  } else if (r >= 0 && uaddr && ulen) {
    u32 zero = 0;
    if (copy_to_user(ulen, &zero, 4)) r = -EFAULT;
  }
  file_put(f);
  return r;
}

/* struct msghdr as laid out by the AArch64 ABI */
struct umsghdr {
  u64 name;
  u32 namelen, pad0;
  u64 iov;
  u32 iovlen, pad1;
  u64 control;
  u32 controllen, pad2;
  s32 flags;
  u32 pad3;
};

struct ucmsg {
  u64 len;
  s32 level, type;
};

#define CMSG_ALIGN(n) (((n) + 7) & ~7UL)

static int load_iov(const struct umsghdr *h, struct iobuf **out, size_t *total) {
  if (h->iovlen > MAX_IOV) return -EMSGSIZE;
  struct iobuf *iov = kmalloc(sizeof(*iov) * (h->iovlen ? h->iovlen : 1), 0);
  if (!iov) return -ENOMEM;
  size_t sum = 0;
  for (u32 i = 0; i < h->iovlen; i++) {
    u64 v[2];
    if (copy_from_user(v, h->iov + i * 16, 16) || !access_ok(v[0], v[1])) {
      kfree(iov);
      return -EFAULT;
    }
    iov[i] = ubuf(v[0], v[1]);
    sum += v[1];
  }
  *out = iov;
  *total = sum;
  return 0;
}

long sys_sendmsg(u64 fd, u64 umsg, u64 flags);
long sys_sendmsg(u64 fd, u64 umsg, u64 flags) {
  struct umsghdr h;
  if (copy_from_user(&h, umsg, sizeof(h))) return -EFAULT;
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  struct kmsg m = {0};
  ssize_t r = load_iov(&h, &m.iov, &m.len);
  m.iovcnt = (int)h.iovlen;
  if (!r && h.name && h.namelen) {
    r = get_addr(h.name, h.namelen, m.name);
    m.namelen = h.namelen;
  }
  /* control messages: SCM_RIGHTS */
  for (u64 off = 0; !r && h.control && off + sizeof(struct ucmsg) <= h.controllen;) {
    struct ucmsg c;
    if (copy_from_user(&c, h.control + off, sizeof(c))) {
      r = -EFAULT;
      break;
    }
    if (c.len < sizeof(c) || off + c.len > h.controllen) {
      r = -EINVAL;
      break;
    }
    if (c.level == SOL_SOCKET && c.type == SCM_RIGHTS) {
      int n = (int)((c.len - sizeof(c)) / 4);
      if (n + m.nfds > SCM_MAX_FD) {
        r = -EINVAL;
        break;
      }
      struct file **nf = kmalloc(sizeof(*nf) * (m.nfds + n + 1), 0);
      if (!nf) {
        r = -ENOMEM;
        break;
      }
      if (m.nfds) memcpy(nf, m.fds, sizeof(*nf) * m.nfds);
      kfree(m.fds);
      m.fds = nf;
      for (int i = 0; i < n; i++) {
        s32 ufd;
        if (copy_from_user(&ufd, h.control + off + sizeof(c) + i * 4, 4)) {
          r = -EFAULT;
          break;
        }
        struct file *xf = fget(ufd);
        if (!xf) {
          r = -EBADF;
          break;
        }
        m.fds[m.nfds++] = xf;
      }
    } else if (c.level == SOL_SOCKET && c.type == SCM_CREDENTIALS) {
      /* credentials are always the kernel-attested sender's */
    } else {
      r = -EINVAL;
    }
    off += CMSG_ALIGN(c.len);
  }
  if (!r)
    r = do_send(s, &m, (int)flags);
  else
    drop_fds(&m);
  kfree(m.iov);
  file_put(f);
  return r;
}

long sys_recvmsg(u64 fd, u64 umsg, u64 flags);
long sys_recvmsg(u64 fd, u64 umsg, u64 flags) {
  struct umsghdr h;
  if (copy_from_user(&h, umsg, sizeof(h))) return -EFAULT;
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  struct kmsg m = {0};
  ssize_t r = load_iov(&h, &m.iov, &m.len);
  m.iovcnt = (int)h.iovlen;
  m.want_creds = s->passcred;
  if (!r) r = s->ops->recvmsg ? s->ops->recvmsg(s, &m, (int)flags) : -EOPNOTSUPP;
  if (r >= 0) {
    u32 cl = 0, oflags = m.flags;
    /* SCM_RIGHTS */
    if (m.nfds) {
      u32 room = h.controllen > sizeof(struct ucmsg) ? (h.controllen - sizeof(struct ucmsg)) / 4 : 0;
      int n = MIN((int)room, m.nfds);
      if (n < m.nfds) oflags |= MSG_CTRUNC;
      if (n > 0) {
        struct ucmsg c = {sizeof(struct ucmsg) + n * 4, SOL_SOCKET, SCM_RIGHTS};
        for (int i = 0; i < n; i++) {
          int nfd = fd_install(m.fds[i], 0, flags & MSG_CMSG_CLOEXEC);
          if (nfd < 0) {
            oflags |= MSG_CTRUNC;
            c.len = sizeof(c) + i * 4;
            break;
          }
          m.fds[i] = NULL; /* now owned by the descriptor table */
          if (copy_to_user(h.control + sizeof(c) + i * 4, &nfd, 4)) r = -EFAULT;
        }
        if (copy_to_user(h.control, &c, sizeof(c))) r = -EFAULT;
        cl = (u32)CMSG_ALIGN(c.len);
      }
    }
    /* SCM_CREDENTIALS */
    if (m.have_creds && m.want_creds) {
      struct ucmsg c = {sizeof(struct ucmsg) + sizeof(struct ucred), SOL_SOCKET, SCM_CREDENTIALS};
      if (cl + c.len <= h.controllen) {
        if (copy_to_user(h.control + cl, &c, sizeof(c)) ||
            copy_to_user(h.control + cl + sizeof(c), &m.creds, sizeof(m.creds)))
          r = -EFAULT;
        cl += (u32)CMSG_ALIGN(c.len);
      } else {
        oflags |= MSG_CTRUNC;
      }
    }
    u32 nl = h.name ? m.namelen : 0;
    if (h.name && nl && copy_to_user(h.name, m.name, MIN(nl, h.namelen))) r = -EFAULT;
    if (copy_to_user(umsg + offsetof(struct umsghdr, namelen), &nl, 4) ||
        copy_to_user(umsg + offsetof(struct umsghdr, controllen), &cl, 4) ||
        copy_to_user(umsg + offsetof(struct umsghdr, flags), &oflags, 4))
      r = -EFAULT;
  }
  drop_fds(&m);
  kfree(m.iov);
  file_put(f);
  return r;
}

long sys_shutdown(u64 fd, u64 how);
long sys_shutdown(u64 fd, u64 how) {
  if (how > SHUT_RDWR) return -EINVAL;
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  err = s->ops->shutdown ? s->ops->shutdown(s, (int)how) : -EOPNOTSUPP;
  file_put(f);
  return err;
}

static int sock_setopt_generic(struct socket *s, int opt, const void *val, u32 len) {
  int v = 0;
  if (opt != SO_RCVTIMEO && opt != SO_SNDTIMEO && opt != SO_LINGER && opt != SO_BINDTODEVICE) {
    if (len < 4) return -EINVAL;
    memcpy(&v, val, 4);
  }
  switch (opt) {
    case SO_REUSEADDR:
    case SO_REUSEPORT:
      s->reuseaddr = v != 0;
      return 0;
    case SO_KEEPALIVE:
      s->keepalive = v != 0;
      return 0;
    case SO_BROADCAST:
      s->broadcast = v != 0;
      return 0;
    case SO_PASSCRED:
      s->passcred = v != 0;
      return 0;
    case SO_SNDBUF:
      s->sndbuf = CLAMP((u32)v * 2, 4096u, 4u << 20);
      return 0;
    case SO_RCVBUF:
      s->rcvbuf = CLAMP((u32)v * 2, 4096u, 4u << 20);
      return 0;
    case SO_RCVTIMEO:
    case SO_SNDTIMEO: {
      if (len < 16) return -EINVAL;
      s64 tv[2];
      memcpy(tv, val, 16);
      if (tv[0] < 0 || tv[1] < 0 || tv[1] >= 1000000) return -EDOM;
      s64 ns = tv[0] * (s64)NSEC_PER_SEC + tv[1] * 1000;
      if (opt == SO_RCVTIMEO)
        s->rcvtimeo_ns = ns;
      else
        s->sndtimeo_ns = ns;
      return 0;
    }
    case SO_LINGER:
    case SO_OOBINLINE:
    case SO_DONTROUTE:
    case SO_RCVLOWAT:
    case SO_SNDLOWAT:
    case SO_TIMESTAMP:
    case SO_BINDTODEVICE:
      return 0; /* accepted, no effect */
    default:
      return -ENOPROTOOPT;
  }
}

long sys_setsockopt(u64 fd, u64 level, u64 opt, u64 uval, u64 len);
long sys_setsockopt(u64 fd, u64 level, u64 opt, u64 uval, u64 len) {
  if (len > 256) return -EINVAL;
  u8 val[256];
  if (len && copy_from_user(val, uval, len)) return -EFAULT;
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  err = -ENOPROTOOPT;
  if (s->ops->setsockopt) err = s->ops->setsockopt(s, (int)level, (int)opt, val, (u32)len);
  if (err == -ENOPROTOOPT && level == SOL_SOCKET) err = sock_setopt_generic(s, (int)opt, val, (u32)len);
  file_put(f);
  return err;
}

static int sock_getopt_generic(struct socket *s, int opt, void *val, u32 *len) {
  int v;
  switch (opt) {
    case SO_TYPE:
      v = s->type;
      break;
    case SO_DOMAIN:
      v = s->family;
      break;
    case SO_PROTOCOL:
      v = s->protocol;
      break;
    case SO_ERROR:
      v = -s->so_error;
      s->so_error = 0;
      break;
    case SO_REUSEADDR:
    case SO_REUSEPORT:
      v = s->reuseaddr;
      break;
    case SO_KEEPALIVE:
      v = s->keepalive;
      break;
    case SO_BROADCAST:
      v = s->broadcast;
      break;
    case SO_PASSCRED:
      v = s->passcred;
      break;
    case SO_ACCEPTCONN:
      v = s->listening;
      break;
    case SO_SNDBUF:
      v = (int)s->sndbuf;
      break;
    case SO_RCVBUF:
      v = (int)s->rcvbuf;
      break;
    case SO_RCVTIMEO:
    case SO_SNDTIMEO: {
      s64 ns = opt == SO_RCVTIMEO ? s->rcvtimeo_ns : s->sndtimeo_ns;
      s64 tv[2] = {ns / (s64)NSEC_PER_SEC, ns % (s64)NSEC_PER_SEC / 1000};
      *len = MIN(*len, 16u);
      memcpy(val, tv, *len);
      return 0;
    }
    case SO_LINGER: {
      int l[2] = {0, 0};
      *len = MIN(*len, 8u);
      memcpy(val, l, *len);
      return 0;
    }
    default:
      return -ENOPROTOOPT;
  }
  *len = MIN(*len, 4u);
  memcpy(val, &v, *len);
  return 0;
}

long sys_getsockopt(u64 fd, u64 level, u64 opt, u64 uval, u64 ulen);
long sys_getsockopt(u64 fd, u64 level, u64 opt, u64 uval, u64 ulen) {
  u32 len;
  if (copy_from_user(&len, ulen, 4)) return -EFAULT;
  if ((s32)len < 0) return -EINVAL;
  if (len > 256) len = 256;
  u8 val[256] = {0};
  int err = 0;
  struct file *f;
  struct socket *s = sockfd((int)fd, &f, &err);
  if (!s) return err;
  err = -ENOPROTOOPT;
  if (s->ops->getsockopt) err = s->ops->getsockopt(s, (int)level, (int)opt, val, &len);
  if (err == -ENOPROTOOPT && level == SOL_SOCKET) err = sock_getopt_generic(s, (int)opt, val, &len);
  if (!err && (copy_to_user(uval, val, len) || copy_to_user(ulen, &len, 4))) err = -EFAULT;
  file_put(f);
  return err;
}
