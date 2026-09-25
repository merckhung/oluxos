/*
 * IPC channels: connected pairs of message endpoints (see
 * include/uapi/olux/ipc.h). Used by userspace services and by the kernel's
 * userfs client. Messages carry a kernel-attested sender identity and may
 * transfer file descriptors.
 */
#include <olux/channel.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/uaccess.h>
#include <uapi/olux/ipc.h>

#define CHAN_MAX_QUEUED 256
#define CHAN_MAX_BYTES (1024 * 1024)

struct chan_msg {
  struct list_head link;
  size_t len;
  int nfds;
  struct file *files[OLUX_MSG_MAX_FDS];
  s32 pid;
  u32 uid, gid;
  u8 data[];
};

struct chan {
  struct list_head q[2]; /* q[i]: messages to be received by side i */
  int nq[2];
  size_t bytes[2];
  bool closed[2];
  struct wait_queue wq[2]; /* wq[i]: side i waiting to receive or to send */
  spinlock_t lock;
  int refs;
};

struct chan_end {
  struct chan *ch;
  int side;
};

static void free_msg(struct chan_msg *m) {
  for (int i = 0; i < m->nfds; i++) file_put(m->files[i]);
  kfree(m);
}

static int chan_release(struct inode *ino, struct file *f) {
  struct chan_end *e = f->priv;
  struct chan *c = e->ch;
  int me = e->side;
  unsigned long fl = spin_lock_irqsave(&c->lock);
  c->closed[me] = true;
  struct list_head dropped;
  list_init(&dropped);
  while (!list_empty(&c->q[me])) {
    struct chan_msg *m = list_first_entry(&c->q[me], struct chan_msg, link);
    list_del(&m->link);
    list_add_tail(&m->link, &dropped);
  }
  c->nq[me] = 0;
  c->bytes[me] = 0;
  bool last = --c->refs == 0;
  spin_unlock_irqrestore(&c->lock, fl);
  while (!list_empty(&dropped)) {
    struct chan_msg *m = list_first_entry(&dropped, struct chan_msg, link);
    list_del(&m->link);
    free_msg(m);
  }
  wake_up(&c->wq[!me]);
  if (last) kfree(c);
  kfree(e);
  return 0;
}

static unsigned chan_poll(struct file *f, struct poll_table *pt) {
  struct chan_end *e = f->priv;
  struct chan *c = e->ch;
  int me = e->side, peer = !me;
  poll_wait(f, &c->wq[me], pt);
  unsigned m = 0;
  if (c->nq[me]) m |= POLLIN | POLLRDNORM;
  if (c->closed[peer])
    m |= POLLHUP;
  else if (c->nq[peer] < CHAN_MAX_QUEUED && c->bytes[peer] < CHAN_MAX_BYTES)
    m |= POLLOUT | POLLWRNORM;
  return m;
}

static const struct file_operations chan_fops = {.release = chan_release, .poll = chan_poll};

bool is_channel(struct file *f) { return f && f->f_op == &chan_fops; }

struct inode *anon_inode(mode_t mode);

int chan_create_pair(struct file **a, struct file **b) {
  struct chan *c = kzalloc(sizeof(*c), 0);
  struct chan_end *ea = kzalloc(sizeof(*ea), 0), *eb = kzalloc(sizeof(*eb), 0);
  struct file *fa = file_alloc(), *fb = file_alloc();
  if (!c || !ea || !eb || !fa || !fb) {
    kfree(c);
    kfree(ea);
    kfree(eb);
    kfree(fa);
    kfree(fb);
    return -ENOMEM;
  }
  for (int i = 0; i < 2; i++) {
    list_init(&c->q[i]);
    wq_init(&c->wq[i]);
  }
  spin_lock_init(&c->lock);
  c->refs = 2;
  ea->ch = eb->ch = c;
  ea->side = 0;
  eb->side = 1;
  struct file *fs[2] = {fa, fb};
  struct chan_end *es[2] = {ea, eb};
  for (int i = 0; i < 2; i++) {
    fs[i]->inode = anon_inode(S_IFSOCK | 0600);
    fs[i]->f_op = &chan_fops;
    fs[i]->priv = es[i];
    fs[i]->mode = FMODE_READ | FMODE_WRITE;
    fs[i]->flags = O_RDWR;
  }
  *a = fa;
  *b = fb;
  return 0;
}

/* Queue a message on `f` for its peer. Takes ownership of `files`. */
static int chan_send(struct file *f, const void *kbuf, u64 ubuf, size_t len, struct file **files, int nfds,
                     bool from_kernel) {
  struct chan_end *e = f->priv;
  struct chan *c = e->ch;
  int peer = !e->side;
  if (len > OLUX_MSG_MAX) return -EMSGSIZE;
  struct chan_msg *m = kmalloc(sizeof(*m) + len, 0);
  if (!m) return -ENOMEM;
  if (kbuf)
    memcpy(m->data, kbuf, len);
  else if (len && copy_from_user(m->data, ubuf, len)) {
    kfree(m);
    return -EFAULT;
  }
  m->len = len;
  m->nfds = nfds;
  for (int i = 0; i < nfds; i++) m->files[i] = files[i];
  struct process *p = from_kernel ? NULL : current->proc;
  m->pid = p ? p->pid : 0;
  m->uid = p ? p->cred.euid : 0;
  m->gid = p ? p->cred.egid : 0;
  for (;;) {
    unsigned long fl = spin_lock_irqsave(&c->lock);
    if (c->closed[peer]) {
      spin_unlock_irqrestore(&c->lock, fl);
      free_msg(m);
      if (!from_kernel) send_signal_thread(current, SIGPIPE, NULL);
      return -EPIPE;
    }
    if (c->nq[peer] < CHAN_MAX_QUEUED && c->bytes[peer] + len <= CHAN_MAX_BYTES) {
      list_add_tail(&m->link, &c->q[peer]);
      c->nq[peer]++;
      c->bytes[peer] += len;
      spin_unlock_irqrestore(&c->lock, fl);
      wake_up(&c->wq[peer]);
      return 0;
    }
    spin_unlock_irqrestore(&c->lock, fl);
    if (f->flags & O_NONBLOCK) {
      m->nfds = 0; /* caller keeps its descriptors */
      kfree(m);
      return -EAGAIN;
    }
    int me = e->side;
    int r = wait_event_interruptible(
        c->wq[me], c->closed[peer] || (c->nq[peer] < CHAN_MAX_QUEUED && c->bytes[peer] + len <= CHAN_MAX_BYTES));
    if (r) {
      m->nfds = 0;
      kfree(m);
      return r;
    }
  }
}

/* Dequeue one message; returns it (caller frees) or an error pointer. */
static struct chan_msg *chan_recv(struct file *f, size_t maxlen, long timeout_ns, size_t *need, bool interruptible) {
  struct chan_end *e = f->priv;
  struct chan *c = e->ch;
  int me = e->side;
  u64 deadline = timeout_ns > 0 ? ktime_ns() + timeout_ns : 0;
  for (;;) {
    unsigned long fl = spin_lock_irqsave(&c->lock);
    if (c->nq[me]) {
      struct chan_msg *m = list_first_entry(&c->q[me], struct chan_msg, link);
      if (m->len > maxlen) {
        *need = m->len;
        spin_unlock_irqrestore(&c->lock, fl);
        return ERR_PTR(-EMSGSIZE);
      }
      list_del(&m->link);
      c->nq[me]--;
      c->bytes[me] -= m->len;
      spin_unlock_irqrestore(&c->lock, fl);
      wake_up(&c->wq[!me]);
      return m;
    }
    bool peer_closed = c->closed[!me];
    spin_unlock_irqrestore(&c->lock, fl);
    if (peer_closed) return ERR_PTR(-EPIPE);
    if (f->flags & O_NONBLOCK) return ERR_PTR(-EAGAIN);
    if (interruptible) {
      long r = wait_event_interruptible_timeout(c->wq[me], c->nq[me] || c->closed[!me],
                                                deadline ? (long)(deadline - MIN(deadline, ktime_ns())) : -1);
      if (r == -ERESTARTSYS) return ERR_PTR(-ERESTARTSYS);
      if (r == 0 && deadline) return ERR_PTR(-ETIMEDOUT);
    } else {
      struct waiter w;
      prepare_to_wait(&c->wq[me], &w, TASK_UNINTERRUPTIBLE);
      if (!c->nq[me] && !c->closed[!me]) {
        if (deadline) {
          u64 now = ktime_ns();
          if (now >= deadline) {
            finish_wait(&c->wq[me], &w);
            return ERR_PTR(-ETIMEDOUT);
          }
          schedule_timeout(deadline - now);
        } else {
          schedule();
        }
      }
      finish_wait(&c->wq[me], &w);
    }
  }
}

/* ---------------- kernel API ---------------- */

int chan_send_kernel(struct file *end, const void *buf, size_t len) {
  if (!is_channel(end)) return -EINVAL;
  return chan_send(end, buf, 0, len, NULL, 0, true);
}

ssize_t chan_recv_kernel(struct file *end, void *buf, size_t len, long timeout_ns, s32 *sender_pid) {
  if (!is_channel(end)) return -EINVAL;
  size_t need;
  struct chan_msg *m = chan_recv(end, len, timeout_ns, &need, false);
  if (IS_ERR(m)) return PTR_ERR(m);
  memcpy(buf, m->data, m->len);
  ssize_t n = m->len;
  if (sender_pid) *sender_pid = m->pid;
  free_msg(m); /* fds sent to the kernel are dropped */
  return n;
}

bool chan_peer_closed(struct file *end) {
  struct chan_end *e = end->priv;
  return e->ch->closed[!e->side];
}

/* ---------------- syscalls ---------------- */

long sys_olux_channel(u64 ufds, u64 flags);
long sys_olux_channel(u64 ufds, u64 flags) {
  if (flags & ~(u64)(O_CLOEXEC | O_NONBLOCK)) return -EINVAL;
  struct file *a, *b;
  int r = chan_create_pair(&a, &b);
  if (r) return r;
  a->flags |= flags & O_NONBLOCK;
  b->flags |= flags & O_NONBLOCK;
  int fa = fd_install(a, 0, flags & O_CLOEXEC);
  if (fa < 0) {
    file_put(a);
    file_put(b);
    return fa;
  }
  int fb = fd_install(b, 0, flags & O_CLOEXEC);
  if (fb < 0) {
    fd_close(fa);
    file_put(b);
    return fb;
  }
  s32 fds[2] = {fa, fb};
  if (copy_to_user(ufds, fds, sizeof(fds))) {
    fd_close(fa);
    fd_close(fb);
    return -EFAULT;
  }
  return 0;
}

long sys_olux_msg_send(u64 fd, u64 buf, u64 len, u64 ufds, u64 nfds);
long sys_olux_msg_send(u64 fd, u64 buf, u64 len, u64 ufds, u64 nfds) {
  if (nfds > OLUX_MSG_MAX_FDS) return -EINVAL;
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  if (!is_channel(f)) {
    file_put(f);
    return -ENOTSOCK;
  }
  struct file *files[OLUX_MSG_MAX_FDS];
  int got = 0;
  long r = 0;
  for (u64 i = 0; i < nfds; i++) {
    s32 xfd;
    if (get_user(xfd, ufds + i * 4)) {
      r = -EFAULT;
      break;
    }
    files[got] = fget(xfd);
    if (!files[got]) {
      r = -EBADF;
      break;
    }
    got++;
  }
  if (!r) r = chan_send(f, NULL, buf, len, files, got, false);
  if (r)
    for (int i = 0; i < got; i++) file_put(files[i]);
  file_put(f);
  return r;
}

long sys_olux_msg_recv(u64 fd, u64 buf, u64 len, u64 uinfo, u64 ufds);
long sys_olux_msg_recv(u64 fd, u64 buf, u64 len, u64 uinfo, u64 ufds) {
  struct file *f = fget((int)fd);
  if (!f) return -EBADF;
  if (!is_channel(f)) {
    file_put(f);
    return -ENOTSOCK;
  }
  size_t need = 0;
  struct chan_msg *m = chan_recv(f, len, -1, &need, true);
  file_put(f);
  struct olux_msg_info info = {0};
  if (IS_ERR(m)) {
    long e = PTR_ERR(m);
    if (e == -EMSGSIZE) {
      info.len = need;
      if (uinfo) copy_to_user(uinfo, &info, sizeof(info));
    }
    if (e == -EPIPE) return 0; /* end of stream */
    return e;
  }
  long r = (long)m->len;
  if (copy_to_user(buf, m->data, m->len)) r = -EFAULT;
  info.len = m->len;
  info.sender_pid = m->pid;
  info.sender_uid = m->uid;
  info.sender_gid = m->gid;
  s32 fds[OLUX_MSG_MAX_FDS];
  int installed = 0;
  if (r >= 0) {
    for (int i = 0; i < m->nfds; i++) {
      int nfd = ufds ? fd_install(m->files[i], 0, false) : -EBADF;
      if (nfd < 0) break;
      m->files[i] = NULL;
      fds[installed++] = nfd;
    }
    info.nfds = installed;
    if (installed && copy_to_user(ufds, fds, installed * sizeof(s32))) r = -EFAULT;
    if (uinfo && copy_to_user(uinfo, &info, sizeof(info))) r = -EFAULT;
  }
  for (int i = installed; i < m->nfds; i++)
    if (m->files[i]) file_put(m->files[i]);
  m->nfds = 0;
  free_msg(m);
  return r;
}
