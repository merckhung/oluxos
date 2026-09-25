#ifndef OLUX_WAIT_H
#define OLUX_WAIT_H

#include <olux/errno.h>
#include <olux/list.h>
#include <olux/spinlock.h>

struct thread;

struct wait_queue {
  spinlock_t lock;
  struct list_head head;
};

struct waiter {
  struct thread *thread;
  struct wait_queue *wq;
  struct list_head link;
};

#define WAIT_QUEUE_INIT(name) {SPINLOCK_INIT, LIST_HEAD_INIT((name).head)}
#define DEFINE_WAIT_QUEUE(name) struct wait_queue name = WAIT_QUEUE_INIT(name)

void wq_init(struct wait_queue *wq);
void prepare_to_wait(struct wait_queue *wq, struct waiter *w, long state);
void finish_wait(struct wait_queue *wq, struct waiter *w);
void wake_up(struct wait_queue *wq);     /* all waiters */
void wake_up_one(struct wait_queue *wq);
bool wq_has_waiters(struct wait_queue *wq);

bool signal_pending_current(void);
void schedule(void);
long schedule_timeout(long ns); /* returns remaining ns (>0) if woken early */

/* Sleep until cond is true. Returns 0, or -ERESTARTSYS if a signal arrived. */
#define wait_event_interruptible(wq, cond)                 \
  ({                                                       \
    int __ret = 0;                                         \
    struct waiter __w;                                     \
    for (;;) {                                             \
      prepare_to_wait(&(wq), &__w, TASK_INTERRUPTIBLE);    \
      if (cond) break;                                     \
      if (signal_pending_current()) {                      \
        __ret = -ERESTARTSYS;                              \
        break;                                             \
      }                                                    \
      schedule();                                          \
    }                                                      \
    finish_wait(&(wq), &__w);                              \
    __ret;                                                 \
  })

#define wait_event(wq, cond)                               \
  do {                                                     \
    struct waiter __w;                                     \
    for (;;) {                                             \
      prepare_to_wait(&(wq), &__w, TASK_UNINTERRUPTIBLE);  \
      if (cond) break;                                     \
      schedule();                                          \
    }                                                      \
    finish_wait(&(wq), &__w);                              \
  } while (0)

/* Returns remaining ns (>0) if cond became true, 0 on timeout,
 * -ERESTARTSYS on signal. timeout_ns < 0 means no timeout. */
#define wait_event_interruptible_timeout(wq, cond, timeout_ns)      \
  ({                                                                \
    long __left = (timeout_ns);                                     \
    long __r;                                                       \
    struct waiter __w;                                              \
    for (;;) {                                                      \
      prepare_to_wait(&(wq), &__w, TASK_INTERRUPTIBLE);             \
      if (cond) {                                                   \
        __r = __left > 0 ? __left : 1;                              \
        break;                                                      \
      }                                                             \
      if (signal_pending_current()) {                               \
        __r = -ERESTARTSYS;                                         \
        break;                                                      \
      }                                                             \
      if (__left == 0) {                                            \
        __r = 0;                                                    \
        break;                                                      \
      }                                                             \
      if (__left < 0) schedule();                                   \
      else __left = schedule_timeout(__left);                       \
    }                                                               \
    finish_wait(&(wq), &__w);                                       \
    __r;                                                            \
  })

/* Completions */
struct completion {
  struct wait_queue wq;
  volatile unsigned done;
};
void init_completion(struct completion *c);
void complete(struct completion *c);
void complete_all(struct completion *c);
void wait_for_completion(struct completion *c);
long wait_for_completion_timeout(struct completion *c, long ns);

/* Sleeping mutex */
struct mutex {
  struct thread *owner;
  struct wait_queue wq;
};
#define MUTEX_INIT(name) {NULL, WAIT_QUEUE_INIT((name).wq)}
#define DEFINE_MUTEX(name) struct mutex name = MUTEX_INIT(name)
void mutex_init(struct mutex *m);
void mutex_lock(struct mutex *m);
int mutex_lock_interruptible(struct mutex *m);
void mutex_unlock(struct mutex *m);

#endif
