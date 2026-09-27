/* Open file objects and per-process descriptor tables. */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>

struct file *file_alloc(void) {
  struct file *f = kzalloc(sizeof(*f), 0);
  if (!f) return NULL;
  atomic_set(&f->refcount, 1);
  mutex_init(&f->pos_lock);
  return f;
}

void file_get(struct file *f) { atomic_inc(&f->refcount); }

void file_put(struct file *f) {
  if (!f || atomic_dec_return(&f->refcount) > 0) return;
  if (f->f_op && f->f_op->release) f->f_op->release(f->inode, f);
  path_put(&f->path);
  inode_put(f->inode);
  kfree(f);
}

struct fdtable *fdtable_alloc(int max) {
  struct fdtable *t = kzalloc(sizeof(*t), 0);
  if (!t) return NULL;
  t->max = max;
  t->fd = kcalloc(max, sizeof(struct file *), 0);
  t->cloexec = kcalloc(DIV_ROUND_UP(max, 64), sizeof(u64), 0);
  if (!t->fd || !t->cloexec) {
    kfree(t->fd);
    kfree(t->cloexec);
    kfree(t);
    return NULL;
  }
  atomic_set(&t->refcount, 1);
  return t;
}

struct fdtable *fdtable_dup(struct fdtable *src) {
  struct fdtable *t = fdtable_alloc(src->max);
  if (!t) return NULL;
  for (int i = 0; i < src->max; i++) {
    if (src->fd[i]) {
      t->fd[i] = src->fd[i];
      file_get(t->fd[i]);
    }
  }
  memcpy(t->cloexec, src->cloexec, DIV_ROUND_UP(src->max, 64) * sizeof(u64));
  return t;
}

void fdtable_put(struct fdtable *t) {
  if (!t || atomic_dec_return(&t->refcount) > 0) return;
  for (int i = 0; i < t->max; i++)
    if (t->fd[i]) file_put(t->fd[i]);
  kfree(t->fd);
  kfree(t->cloexec);
  kfree(t);
}

void fdtable_close_on_exec(struct fdtable *t) {
  for (int i = 0; i < t->max; i++) {
    if (t->fd[i] && (t->cloexec[i / 64] & (1UL << (i % 64)))) {
      file_put(t->fd[i]);
      t->fd[i] = NULL;
      t->cloexec[i / 64] &= ~(1UL << (i % 64));
    }
  }
}

static struct fdtable *cur_table(void) { return current->proc ? current->proc->files : NULL; }

struct file *fget(int fd) {
  struct fdtable *t = cur_table();
  if (!t || fd < 0 || fd >= t->max || !t->fd[fd]) return NULL;
  file_get(t->fd[fd]);
  return t->fd[fd];
}

int fd_install(struct file *f, int min_fd, bool cloexec) {
  struct fdtable *t = cur_table();
  if (!t) return -EBADF;
  int limit = t->max;
  struct process *p = current->proc;
  if (p->rlim[RLIMIT_NOFILE].cur < (u64)limit) limit = (int)p->rlim[RLIMIT_NOFILE].cur;
  for (int i = min_fd; i < limit; i++) {
    if (!t->fd[i]) {
      t->fd[i] = f;
      if (cloexec) t->cloexec[i / 64] |= 1UL << (i % 64);
      else t->cloexec[i / 64] &= ~(1UL << (i % 64));
      return i;
    }
  }
  return -EMFILE;
}

int fd_close(int fd) {
  struct fdtable *t = cur_table();
  if (!t || fd < 0 || fd >= t->max || !t->fd[fd]) return -EBADF;
  struct file *f = t->fd[fd];
  t->fd[fd] = NULL;
  t->cloexec[fd / 64] &= ~(1UL << (fd % 64));
  file_put(f);
  return 0;
}
