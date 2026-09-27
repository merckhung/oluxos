/* Anonymous pipes and FIFOs (64 KiB ring buffer, POSIX atomicity for
 * writes up to PIPE_BUF, SIGPIPE/EPIPE when there are no readers). */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/uaccess.h>

#define PIPE_SIZE (64 * 1024)
#define PIPE_BUF 4096

struct pipe {
  u8 *buf;
  size_t head, tail; /* free-running */
  int readers, writers;
  struct wait_queue rwait, wwait;
  struct inode *inode;
  int w_counter; /* incremented on each writer open (FIFO open wakeups) */
};

static struct super_block pipe_sb;
static const struct file_operations pipe_fops;

static struct pipe *pipe_alloc(void) {
  struct pipe *p = kzalloc(sizeof(*p), 0);
  if (!p) return NULL;
  p->buf = get_free_pages(0, 4); /* 64 KiB */
  if (!p->buf) {
    kfree(p);
    return NULL;
  }
  wq_init(&p->rwait);
  wq_init(&p->wwait);
  return p;
}

static void pipe_free(struct pipe *p) {
  free_pages_va(p->buf, 4);
  kfree(p);
}

static size_t pipe_used(struct pipe *p) { return p->head - p->tail; }

static ssize_t pipe_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct pipe *p = f->inode->pipe;
  for (;;) {
    if (pipe_used(p)) break;
    if (!p->writers) return 0;
    if (f->flags & O_NONBLOCK) return -EAGAIN;
    int r = wait_event_interruptible(p->rwait, pipe_used(p) || !p->writers);
    if (r) return r;
  }
  size_t done = 0;
  while (done < b->len && pipe_used(p)) {
    size_t off = p->tail % PIPE_SIZE;
    size_t n = MIN(MIN(b->len - done, pipe_used(p)), (size_t)PIPE_SIZE - off);
    if (iob_write(b, done, p->buf + off, n)) {
      if (!done) return -EFAULT;
      break;
    }
    p->tail += n;
    done += n;
  }
  wake_up(&p->wwait);
  return done;
}

static ssize_t pipe_write(struct file *f, struct iobuf *b, loff_t *pos) {
  struct pipe *p = f->inode->pipe;
  size_t done = 0;
  while (done < b->len) {
    if (!p->readers) {
      send_signal_thread(current, SIGPIPE, NULL);
      return done ? (ssize_t)done : -EPIPE;
    }
    size_t space = PIPE_SIZE - pipe_used(p);
    size_t want = b->len - done;
    /* writes <= PIPE_BUF are atomic */
    if (space == 0 || (want <= PIPE_BUF && space < want)) {
      if (f->flags & O_NONBLOCK) return done ? (ssize_t)done : -EAGAIN;
      wake_up(&p->rwait);
      int r = wait_event_interruptible(p->wwait, !p->readers || PIPE_SIZE - pipe_used(p) >= MIN(want, (size_t)PIPE_BUF));
      if (r) return done ? (ssize_t)done : r;
      continue;
    }
    size_t off = p->head % PIPE_SIZE;
    size_t n = MIN(MIN(want, space), (size_t)PIPE_SIZE - off);
    if (iob_read(b, done, p->buf + off, n)) return done ? (ssize_t)done : -EFAULT;
    p->head += n;
    done += n;
    wake_up(&p->rwait);
  }
  f->inode->mtime = current_time();
  return done;
}

static unsigned pipe_poll(struct file *f, struct poll_table *pt) {
  struct pipe *p = f->inode->pipe;
  unsigned m = 0;
  if (f->mode & FMODE_READ) {
    poll_wait(f, &p->rwait, pt);
    if (pipe_used(p)) m |= POLLIN | POLLRDNORM;
    if (!p->writers && p->w_counter) m |= POLLHUP;
  }
  if (f->mode & FMODE_WRITE) {
    poll_wait(f, &p->wwait, pt);
    if (PIPE_SIZE - pipe_used(p) >= PIPE_BUF) m |= POLLOUT | POLLWRNORM;
    if (!p->readers) m |= POLLERR;
  }
  return m;
}

static long pipe_ioctl(struct file *f, unsigned cmd, u64 arg) {
  if (cmd == 0x541b /* FIONREAD */) {
    s32 n = (s32)pipe_used(f->inode->pipe);
    return copy_to_user(arg, &n, sizeof(n));
  }
  return -ENOTTY;
}

static int pipe_release(struct inode *i, struct file *f) {
  struct pipe *p = i->pipe;
  if (f->mode & FMODE_READ) p->readers--;
  if (f->mode & FMODE_WRITE) p->writers--;
  wake_up(&p->rwait);
  wake_up(&p->wwait);
  if (!p->readers && !p->writers) {
    /* last user gone: named FIFOs get a fresh buffer on the next open */
    i->pipe = NULL;
    pipe_free(p);
  }
  return 0;
}

static const struct file_operations pipe_fops = {
    .read = pipe_read, .write = pipe_write, .poll = pipe_poll, .ioctl = pipe_ioctl, .release = pipe_release};

struct file *pipe_create_pair(struct file **wr, int flags) {
  if (!pipe_sb.type) {
    static struct fs_type pipefs = {.name = "pipefs"};
    pipe_sb.type = &pipefs;
    list_init(&pipe_sb.inodes);
  }
  struct inode *i = new_inode(&pipe_sb, S_IFIFO | 0600);
  if (!i) return NULL;
  struct pipe *p = pipe_alloc();
  if (!p) {
    inode_put(i);
    return NULL;
  }
  i->pipe = p;
  i->f_op = &pipe_fops;
  p->inode = i;
  struct file *r = file_alloc(), *w = file_alloc();
  if (!r || !w) {
    kfree(r);
    kfree(w);
    pipe_free(p);
    inode_put(i);
    return NULL;
  }
  r->inode = i;
  w->inode = i;
  inode_get(i); /* second file reference; first consumed by r */
  r->f_op = w->f_op = &pipe_fops;
  r->mode = FMODE_READ;
  w->mode = FMODE_WRITE;
  r->flags = O_RDONLY | (flags & O_NONBLOCK);
  w->flags = O_WRONLY | (flags & O_NONBLOCK);
  p->readers = p->writers = 1;
  p->w_counter = 1;
  *wr = w;
  return r;
}

/* Opening a named FIFO blocks until the other end is opened (POSIX). */
int fifo_open(struct inode *inode, struct file *f) {
  if (!inode->pipe) {
    struct pipe *p = pipe_alloc();
    if (!p) return -ENOMEM;
    p->inode = inode;
    inode->pipe = p;
  }
  struct pipe *p = inode->pipe;
  f->f_op = &pipe_fops;
  if (f->mode & FMODE_READ) {
    p->readers++;
    wake_up(&p->wwait);
    if (!(f->mode & FMODE_WRITE) && !(f->flags & O_NONBLOCK) && !p->writers) {
      int c = p->w_counter;
      int r = wait_event_interruptible(p->rwait, p->writers || p->w_counter != c);
      if (r) {
        p->readers--;
        return r;
      }
    }
  }
  if (f->mode & FMODE_WRITE) {
    if (!(f->mode & FMODE_READ) && (f->flags & O_NONBLOCK) && !p->readers) return -ENXIO;
    p->writers++;
    p->w_counter++;
    wake_up(&p->rwait);
    if (!(f->mode & FMODE_READ) && !p->readers) {
      int r = wait_event_interruptible(p->wwait, p->readers > 0);
      if (r) {
        p->writers--;
        return r;
      }
    }
  }
  return 0;
}
