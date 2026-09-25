/*
 * AF_UNIX sockets: SOCK_STREAM, SOCK_SEQPACKET and SOCK_DGRAM; filesystem
 * and abstract (leading NUL) addresses; socketpair; SCM_RIGHTS descriptor
 * passing; SO_PEERCRED and SCM_CREDENTIALS (kernel-attested).
 *
 * All AF_UNIX state is protected by one mutex. Every socket waits only on
 * its own wait queue: a sender wakes the receiver's queue after queueing
 * data, and a receiver wakes its peer's queue after freeing buffer space.
 */
#include <olux/device.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/net.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/uaccess.h>

#define UNIX_PATH_MAX 108
#define FIONREAD 0x541b
#define TIOCOUTQ 0x5411

enum { U_UNCONNECTED, U_LISTENING, U_CONNECTED, U_CLOSED };

struct uchunk {
  struct list_head link;
  size_t len, off;
  struct file **fds;
  int nfds;
  struct ucred creds;
  u8 from[2 + UNIX_PATH_MAX]; /* dgram: sender address */
  u32 fromlen;
  u8 data[];
};

struct usock {
  struct socket *sock;
  int state;
  int refs; /* owner + dgram peers that connected to us */
  struct usock *peer;
  struct list_head rq;
  size_t rq_bytes;
  bool shut_rd, shut_wr, peer_gone;
  /* listening */
  struct list_head backlog; /* embryos */
  int nbacklog, max_backlog;
  struct list_head bl_link;
  /* address */
  u8 addr[2 + UNIX_PATH_MAX];
  u32 addrlen;         /* 0 = unbound */
  struct inode *inode; /* filesystem-bound */
  struct list_head bound_link;
  struct ucred peercred;
  bool have_peercred;
};

static struct mutex ulock;
static LIST_HEAD(bound);

static struct usock *us(struct socket *s) { return s->priv; }

static void uput(struct usock *u) {
  if (--u->refs == 0) kfree(u);
}

static void free_chunk(struct uchunk *c) {
  for (int i = 0; i < c->nfds; i++) file_put(c->fds[i]);
  kfree(c->fds);
  kfree(c);
}

static void flush_rq(struct usock *u) {
  while (!list_empty(&u->rq)) {
    struct uchunk *c = list_first_entry(&u->rq, struct uchunk, link);
    list_del(&c->link);
    free_chunk(c);
  }
  u->rq_bytes = 0;
}

static struct ucred my_creds(void) {
  struct process *p = current->proc;
  return (struct ucred){p->pid, p->cred.euid, p->cred.egid};
}

static void wake(struct usock *u) {
  if (u && u->sock) wake_up(&u->sock->wq);
}

/* ---------------- addresses ---------------- */

static struct usock *find_bound(const u8 *addr, u32 len, struct inode *ino) {
  struct usock *u;
  list_for_each_entry(u, &bound, bound_link) {
    if (ino ? u->inode == ino : (addr && !u->inode && u->addrlen == len && !memcmp(u->addr, addr, len))) return u;
  }
  return NULL;
}

/* Resolve a destination address to a live socket (referenced inode kept). */
static int lookup(const u8 *addr, u32 len, struct usock **out) {
  if (len <= 2 || ((const u16 *)addr)[0] != AF_UNIX) return -EINVAL;
  if (len > 2 + UNIX_PATH_MAX) len = 2 + UNIX_PATH_MAX;
  struct usock *t;
  if (addr[2] == 0) { /* abstract */
    t = find_bound(addr, len, NULL);
  } else {
    char path[UNIX_PATH_MAX + 1];
    size_t n = strnlen((const char *)addr + 2, len - 2);
    memcpy(path, addr + 2, n);
    path[n] = 0;
    struct path p;
    int r = kern_path(path, LOOKUP_FOLLOW, &p);
    if (r) return r;
    if (!S_ISSOCK(p.dentry->inode->mode)) {
      path_put(&p);
      return -ECONNREFUSED;
    }
    t = find_bound(NULL, 0, p.dentry->inode);
    path_put(&p);
  }
  if (!t || t->state == U_CLOSED) return -ECONNREFUSED;
  *out = t;
  return 0;
}

static int u_bind(struct socket *s, const void *a, u32 len) {
  const u8 *addr = a;
  struct usock *u = us(s);
  if (len < 2 || ((const u16 *)addr)[0] != AF_UNIX) return -EINVAL;
  if (len > 2 + UNIX_PATH_MAX) return -EINVAL;
  if (u->addrlen) return -EINVAL;
  if (len == 2) return 0; /* autobind: stay unnamed */
  int r = 0;
  mutex_lock(&ulock);
  if (addr[2] == 0) {
    if (find_bound(addr, len, NULL)) r = -EADDRINUSE;
  } else {
    char path[UNIX_PATH_MAX + 1];
    size_t n = strnlen((const char *)addr + 2, len - 2);
    memcpy(path, addr + 2, n);
    path[n] = 0;
    len = (u32)(2 + n + 1);
    mutex_unlock(&ulock);
    r = vfs_mknod(AT_FDCWD, path, S_IFSOCK | (0777 & ~current->proc->umask), 0);
    if (r == -EEXIST) r = -EADDRINUSE;
    struct path p;
    if (!r) r = kern_path(path, 0, &p);
    mutex_lock(&ulock);
    if (!r) {
      u->inode = p.dentry->inode;
      inode_get(u->inode);
      path_put(&p);
    }
  }
  if (!r) {
    memcpy(u->addr, addr, len);
    u->addrlen = len;
    list_add(&u->bound_link, &bound);
  }
  mutex_unlock(&ulock);
  return r;
}

static int u_getname(struct socket *s, void *addr, u32 *len, bool peer) {
  struct usock *u = us(s);
  mutex_lock(&ulock);
  struct usock *t = peer ? u->peer : u;
  int r = 0;
  if (peer && !t) {
    r = -ENOTCONN;
  } else if (t->addrlen) {
    memcpy(addr, t->addr, t->addrlen);
    *len = t->addrlen;
  } else {
    *(u16 *)addr = AF_UNIX;
    *len = 2;
  }
  mutex_unlock(&ulock);
  return r;
}

/* ---------------- connection management ---------------- */

static struct usock *usock_new(struct socket *s) {
  struct usock *u = kzalloc(sizeof(*u), 0);
  if (!u) return NULL;
  u->sock = s;
  u->refs = 1;
  list_init(&u->rq);
  list_init(&u->backlog);
  list_init(&u->bound_link);
  list_init(&u->bl_link);
  s->priv = u;
  return u;
}

static const struct proto_ops unix_ops;

static void pair(struct usock *a, struct usock *b) {
  a->peer = b;
  b->peer = a;
  a->state = b->state = U_CONNECTED;
}

int unix_socketpair(struct socket *a, struct socket *b);
int unix_socketpair(struct socket *a, struct socket *b) {
  mutex_lock(&ulock);
  struct usock *ua = us(a), *ub = us(b);
  pair(ua, ub);
  if (a->type == SOCK_DGRAM) { /* dgram peers hold references on each other */
    ua->refs++;
    ub->refs++;
  }
  ua->peercred = ub->peercred = my_creds();
  ua->have_peercred = ub->have_peercred = true;
  mutex_unlock(&ulock);
  return 0;
}

static int u_release(struct socket *s) {
  struct usock *u = us(s);
  if (!u) return 0;
  mutex_lock(&ulock);
  u->state = U_CLOSED;
  if (!list_empty(&u->bound_link) || u->addrlen) {
    list_del(&u->bound_link);
    list_init(&u->bound_link);
  }
  struct inode *ino = u->inode;
  u->inode = NULL;
  /* pending, never-accepted connections */
  while (!list_empty(&u->backlog)) {
    struct usock *e = list_first_entry(&u->backlog, struct usock, bl_link);
    list_del(&e->bl_link);
    mutex_unlock(&ulock);
    sock_free(e->sock); /* the client sees end-of-file */
    mutex_lock(&ulock);
  }
  struct usock *p = u->peer;
  if (p) {
    if (s->type == SOCK_DGRAM) {
      if (p->peer == u) { /* socketpair: break both directions */
        p->peer = NULL;
        uput(u);
      }
      wake(p);
      uput(p); /* may free p */
    } else {
      p->peer = NULL;
      p->peer_gone = true;
      wake(p);
    }
    u->peer = NULL;
  }
  flush_rq(u);
  u->sock = NULL;
  s->priv = NULL;
  uput(u);
  mutex_unlock(&ulock);
  if (ino) inode_put(ino);
  return 0;
}

static int u_listen(struct socket *s, int backlog) {
  struct usock *u = us(s);
  if (s->type == SOCK_DGRAM) return -EOPNOTSUPP;
  mutex_lock(&ulock);
  int r = 0;
  if (!u->addrlen)
    r = -EINVAL; /* Linux autobinds; require an explicit bind */
  else if (u->state != U_UNCONNECTED && u->state != U_LISTENING)
    r = -EINVAL;
  else {
    u->state = U_LISTENING;
    u->max_backlog = backlog;
    u->peercred = s->owner;
  }
  mutex_unlock(&ulock);
  return r;
}

static int u_create(struct socket *s, int type, int protocol);

static int u_connect(struct socket *s, const void *addr, u32 len) {
  struct usock *u = us(s);
  if (len >= 2 && ((const u16 *)addr)[0] == AF_UNSPEC && s->type == SOCK_DGRAM) { /* dissolve */
    mutex_lock(&ulock);
    if (u->peer) uput(u->peer);
    u->peer = NULL;
    mutex_unlock(&ulock);
    return 0;
  }
  mutex_lock(&ulock);
  struct usock *t;
  int r = lookup(addr, len, &t);
  if (r) goto out;
  if (t->sock->type != s->type) {
    r = -EPROTOTYPE;
    goto out;
  }
  if (s->type == SOCK_DGRAM) {
    if (u->peer) uput(u->peer);
    u->peer = t;
    t->refs++;
    goto out;
  }
  if (u->state == U_CONNECTED) {
    r = -EISCONN;
    goto out;
  }
  if (t->state != U_LISTENING) {
    r = -ECONNREFUSED;
    goto out;
  }
  while (t->nbacklog >= t->max_backlog) {
    bool nb = sock_nonblock(s, 0);
    mutex_unlock(&ulock);
    if (nb) return -EAGAIN;
    sleep_ns(10 * NSEC_PER_MSEC);
    if (signal_pending_current()) return -ERESTARTSYS;
    mutex_lock(&ulock);
    if (t->state != U_LISTENING) {
      r = -ECONNREFUSED;
      goto out;
    }
  }
  /* the server-side socket, handed out by accept() */
  struct socket *es = sock_alloc(AF_UNIX, s->type, 0);
  if (!es || u_create(es, s->type, 0)) {
    kfree(es);
    r = -ENOMEM;
    goto out;
  }
  struct usock *e = us(es);
  pair(u, e);
  memcpy(e->addr, t->addr, t->addrlen);
  e->addrlen = t->addrlen;
  e->peercred = my_creds();
  u->peercred = t->peercred;
  e->have_peercred = u->have_peercred = true;
  es->owner = t->sock->owner;
  list_add_tail(&e->bl_link, &t->backlog);
  t->nbacklog++;
  wake(t);
out:
  mutex_unlock(&ulock);
  return r;
}

static int u_accept(struct socket *s, struct socket **out) {
  struct usock *u = us(s);
  if (u->state != U_LISTENING) return -EINVAL;
  for (;;) {
    mutex_lock(&ulock);
    if (!list_empty(&u->backlog)) {
      struct usock *e = list_first_entry(&u->backlog, struct usock, bl_link);
      list_del(&e->bl_link);
      list_init(&e->bl_link);
      u->nbacklog--;
      mutex_unlock(&ulock);
      *out = e->sock;
      return 0;
    }
    mutex_unlock(&ulock);
    int r = sock_wait(s, !list_empty(&u->backlog) || u->state != U_LISTENING, sock_nonblock(s, 0), s->rcvtimeo_ns);
    if (r) return r;
    if (u->state != U_LISTENING) return -EINVAL;
  }
}

static int u_shutdown(struct socket *s, int how) {
  struct usock *u = us(s);
  mutex_lock(&ulock);
  int r = 0;
  if (u->state != U_CONNECTED && s->type != SOCK_DGRAM) r = -ENOTCONN;
  if (!r) {
    if (how != SHUT_WR) u->shut_rd = true;
    if (how != SHUT_RD) u->shut_wr = true;
    wake(u);
    wake(u->peer);
  }
  mutex_unlock(&ulock);
  return r;
}

/* ---------------- data ---------------- */

static bool peer_has_room(struct usock *u, struct usock *t) { return t && t->rq_bytes < t->sock->rcvbuf; }

static struct uchunk *make_chunk(struct kmsg *m, size_t off, size_t len, int *err) {
  struct uchunk *c = kmalloc(sizeof(*c) + len, 0);
  if (!c) {
    *err = -ENOBUFS;
    return NULL;
  }
  memset(c, 0, sizeof(*c));
  c->len = len;
  if (kmsg_copy_from(m, off, c->data, len)) {
    kfree(c);
    *err = -EFAULT;
    return NULL;
  }
  c->creds = my_creds();
  return c;
}

static void attach_fds(struct uchunk *c, struct kmsg *m) {
  if (!m->nfds) return;
  c->fds = m->fds; /* take ownership */
  c->nfds = m->nfds;
  m->fds = NULL;
  m->nfds = 0;
}

static ssize_t send_stream(struct socket *s, struct kmsg *m, int flags) {
  struct usock *u = us(s);
  size_t done = 0;
  bool seq = s->type == SOCK_SEQPACKET;
  if (seq && m->len > s->sndbuf) return -EMSGSIZE;
  while (done < m->len || (m->len == 0 && done == 0)) {
    mutex_lock(&ulock);
    struct usock *t = u->peer;
    if (u->shut_wr || u->peer_gone || !t || t->shut_rd) {
      mutex_unlock(&ulock);
      if (u->state != U_CONNECTED && !u->peer_gone) return done ? (ssize_t)done : -ENOTCONN;
      return done ? (ssize_t)done : -EPIPE;
    }
    if (!peer_has_room(u, t)) {
      mutex_unlock(&ulock);
      if (done && sock_nonblock(s, flags)) return done;
      int r = sock_wait(s, u->peer_gone || !u->peer || u->shut_wr || peer_has_room(u, u->peer), sock_nonblock(s, flags),
                        s->sndtimeo_ns);
      if (r) return done ? (ssize_t)done : r;
      continue;
    }
    size_t n = seq ? m->len : MIN(m->len - done, (size_t)65536);
    int err = 0;
    struct uchunk *c = make_chunk(m, done, n, &err);
    if (!c) {
      mutex_unlock(&ulock);
      return done ? (ssize_t)done : err;
    }
    if (done == 0) attach_fds(c, m);
    list_add_tail(&c->link, &t->rq);
    t->rq_bytes += n;
    wake(t);
    mutex_unlock(&ulock);
    done += n;
    if (m->len == 0) break;
  }
  return done;
}

static ssize_t send_dgram(struct socket *s, struct kmsg *m, int flags) {
  struct usock *u = us(s);
  if (m->len > s->sndbuf) return -EMSGSIZE;
  for (;;) {
    mutex_lock(&ulock);
    struct usock *t = u->peer;
    int r = 0;
    if (m->namelen)
      r = lookup(m->name, m->namelen, &t);
    else if (!t)
      r = -ENOTCONN;
    if (!r && !t) r = -ENOTCONN; /* (lookup() sets t on success) */
    if (!r && (t->state == U_CLOSED || t->shut_rd)) r = -ECONNREFUSED;
    if (!r && t->sock->type != SOCK_DGRAM) r = -EPROTOTYPE;
    if (r) {
      mutex_unlock(&ulock);
      return r;
    }
    if (t->rq_bytes + m->len > t->sock->rcvbuf && t->rq_bytes) {
      mutex_unlock(&ulock);
      if (sock_nonblock(s, flags)) return -EAGAIN;
      sleep_ns(5 * NSEC_PER_MSEC); /* receiver space is not signalled to anonymous senders */
      if (signal_pending_current()) return -ERESTARTSYS;
      continue;
    }
    int err = 0;
    struct uchunk *c = make_chunk(m, 0, m->len, &err);
    if (!c) {
      mutex_unlock(&ulock);
      return err;
    }
    attach_fds(c, m);
    if (u->addrlen) {
      memcpy(c->from, u->addr, u->addrlen);
      c->fromlen = u->addrlen;
    }
    list_add_tail(&c->link, &t->rq);
    t->rq_bytes += m->len;
    wake(t);
    mutex_unlock(&ulock);
    return m->len;
  }
}

static ssize_t u_sendmsg(struct socket *s, struct kmsg *m, int flags) {
  if (flags & MSG_OOB) return -EOPNOTSUPP;
  if (s->type == SOCK_DGRAM) return send_dgram(s, m, flags);
  if (m->namelen && us(s)->state == U_CONNECTED) return -EISCONN;
  return send_stream(s, m, flags);
}

static ssize_t u_recvmsg(struct socket *s, struct kmsg *m, int flags) {
  struct usock *u = us(s);
  if (flags & MSG_OOB) return -EOPNOTSUPP;
  bool stream = s->type == SOCK_STREAM;
  size_t done = 0;
  for (;;) {
    mutex_lock(&ulock);
    if (list_empty(&u->rq)) {
      bool eof = u->shut_rd || u->peer_gone || (u->peer && u->peer->shut_wr) ||
                 (stream && u->state != U_CONNECTED && u->state != U_UNCONNECTED);
      bool notconn = s->type != SOCK_DGRAM && u->state == U_UNCONNECTED;
      mutex_unlock(&ulock);
      if (done && (!(flags & MSG_WAITALL) || eof)) return done;
      if (eof) return done;
      if (notconn) return -ENOTCONN;
      if (done && sock_nonblock(s, flags)) return done;
      int r = sock_wait(s, !list_empty(&u->rq) || u->shut_rd || u->peer_gone || (u->peer && u->peer->shut_wr),
                        sock_nonblock(s, flags), s->rcvtimeo_ns);
      if (r) return done ? (ssize_t)done : r;
      continue;
    }
    struct uchunk *c = list_first_entry(&u->rq, struct uchunk, link);
    if (stream && done && c->nfds) { /* descriptors travel with their own read */
      mutex_unlock(&ulock);
      return done;
    }
    size_t avail = c->len - c->off, want = m->len - done;
    size_t n = MIN(avail, want);
    if (kmsg_copy_to(m, done, c->data + c->off, n)) {
      mutex_unlock(&ulock);
      return done ? (ssize_t)done : -EFAULT;
    }
    if (c->nfds && !(flags & MSG_PEEK)) {
      m->fds = c->fds;
      m->nfds = c->nfds;
      c->fds = NULL;
      c->nfds = 0;
    }
    if (!done) {
      m->creds = c->creds;
      m->have_creds = true;
      if (c->fromlen) {
        memcpy(m->name, c->from, c->fromlen);
        m->namelen = c->fromlen;
      } else if (s->type == SOCK_DGRAM) {
        *(u16 *)m->name = AF_UNIX;
        m->namelen = 2;
      }
    }
    done += n;
    if (!stream) { /* one record per call */
      size_t full = c->len;
      if (n < avail) m->flags |= MSG_TRUNC;
      if (!(flags & MSG_PEEK)) {
        list_del(&c->link);
        u->rq_bytes -= c->len;
        free_chunk(c);
      }
      wake(u->peer);
      mutex_unlock(&ulock);
      return (flags & MSG_TRUNC) ? (ssize_t)full : (ssize_t)done;
    }
    if (!(flags & MSG_PEEK)) {
      c->off += n;
      u->rq_bytes -= n;
      if (c->off == c->len) {
        list_del(&c->link);
        free_chunk(c);
      }
      wake(u->peer);
    }
    mutex_unlock(&ulock);
    if (done == m->len || (flags & MSG_PEEK)) return done;
  }
}

static unsigned u_poll(struct socket *s, struct file *f, struct poll_table *pt) {
  struct usock *u = us(s);
  poll_wait(f, &s->wq, pt);
  mutex_lock(&ulock);
  unsigned m = 0;
  if (u->state == U_LISTENING) {
    if (!list_empty(&u->backlog)) m |= POLLIN | POLLRDNORM;
  } else {
    bool hup = u->peer_gone || (u->shut_rd && u->shut_wr);
    if (!list_empty(&u->rq) || u->shut_rd || hup || (u->peer && u->peer->shut_wr)) m |= POLLIN | POLLRDNORM;
    if (hup) m |= POLLHUP;
    if (u->shut_rd || (u->peer && u->peer->shut_wr) || u->peer_gone) m |= POLLRDHUP;
    if (s->type == SOCK_DGRAM) {
      if (!u->peer || peer_has_room(u, u->peer)) m |= POLLOUT | POLLWRNORM;
    } else if (u->state == U_CONNECTED && !u->shut_wr && u->peer && peer_has_room(u, u->peer)) {
      m |= POLLOUT | POLLWRNORM;
    } else if (hup) {
      m |= POLLOUT; /* writing reports the error */
    }
  }
  mutex_unlock(&ulock);
  return m;
}

static long u_ioctl(struct socket *s, unsigned cmd, u64 arg) {
  struct usock *u = us(s);
  int v = 0;
  switch (cmd) {
    case FIONREAD:
      mutex_lock(&ulock);
      if (s->type == SOCK_STREAM)
        v = (int)u->rq_bytes;
      else if (!list_empty(&u->rq))
        v = (int)list_first_entry(&u->rq, struct uchunk, link)->len;
      mutex_unlock(&ulock);
      return put_user(v, (int *)arg);
    case TIOCOUTQ:
      return put_user(0, (int *)arg);
    default:
      return -ENOTTY;
  }
}

static int u_getsockopt(struct socket *s, int level, int opt, void *val, u32 *len) {
  struct usock *u = us(s);
  if (level == SOL_SOCKET && opt == SO_PEERCRED) {
    if (!u->have_peercred) return -ENOTCONN;
    *len = MIN(*len, (u32)sizeof(struct ucred));
    memcpy(val, &u->peercred, *len);
    return 0;
  }
  return -ENOPROTOOPT;
}

static const struct proto_ops unix_ops = {
    .release = u_release,
    .bind = u_bind,
    .connect = u_connect,
    .listen = u_listen,
    .accept = u_accept,
    .sendmsg = u_sendmsg,
    .recvmsg = u_recvmsg,
    .shutdown = u_shutdown,
    .getname = u_getname,
    .getsockopt = u_getsockopt,
    .poll = u_poll,
    .ioctl = u_ioctl,
};

static int u_create(struct socket *s, int type, int protocol) {
  if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_SEQPACKET) return -ESOCKTNOSUPPORT;
  if (protocol != 0 && protocol != AF_UNIX) return -EPROTONOSUPPORT;
  if (!usock_new(s)) return -ENOMEM;
  s->ops = &unix_ops;
  return 0;
}

static void show_unix(seq_printf_t pr, void *ctx) {
  pr(ctx, "Num       RefCount Protocol Flags    Type St Inode Path\n");
  mutex_lock(&ulock);
  struct usock *u;
  list_for_each_entry(u, &bound, bound_link) {
    int st = u->state == U_LISTENING ? 1 : u->state == U_CONNECTED ? 3 : 1;
    int type = u->sock ? u->sock->type : SOCK_STREAM;
    char path[UNIX_PATH_MAX + 2];
    size_t n = u->addrlen > 2 ? u->addrlen - 2 : 0;
    memcpy(path, u->addr + 2, n);
    path[n] = 0;
    if (n && path[0] == 0) path[0] = '@'; /* abstract */
    pr(ctx, "%016lx: %08X %08X %08X %04X %02X %5d %s\n", (unsigned long)(uintptr_t)u, u->refs, 0,
       u->state == U_LISTENING ? 0x10000 : 0, type, st, 0, path);
  }
  mutex_unlock(&ulock);
}

static const struct net_family unix_family = {.family = AF_UNIX, .create = u_create};

static int unix_init(void) {
  mutex_init(&ulock);
  net_register_family(&unix_family);
  proc_net_register("unix", show_unix);
  return 0;
}
core_initcall(unix_init);
