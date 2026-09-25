/* Wait queues, completions and sleeping mutexes. */
#include <olux/kernel.h>
#include <olux/sched.h>
#include <olux/wait.h>

void wq_init(struct wait_queue *wq) {
  spin_lock_init(&wq->lock);
  list_init(&wq->head);
}

void prepare_to_wait(struct wait_queue *wq, struct waiter *w, long state) {
  unsigned long f = spin_lock_irqsave(&wq->lock);
  if (w->wq != wq || !list_linked(&w->link)) {
    w->thread = current;
    w->wq = wq;
    list_init(&w->link);
    list_add_tail(&w->link, &wq->head);
  }
  current->state = state;
  spin_unlock_irqrestore(&wq->lock, f);
}

void finish_wait(struct wait_queue *wq, struct waiter *w) {
  current->state = TASK_RUNNING;
  unsigned long f = spin_lock_irqsave(&wq->lock);
  if (w->wq == wq && list_linked(&w->link)) list_del(&w->link);
  w->wq = NULL;
  spin_unlock_irqrestore(&wq->lock, f);
}

void wake_up(struct wait_queue *wq) {
  unsigned long f = spin_lock_irqsave(&wq->lock);
  struct waiter *w;
  list_for_each_entry(w, &wq->head, link) try_to_wake_up(w->thread, TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE);
  spin_unlock_irqrestore(&wq->lock, f);
}

void wake_up_one(struct wait_queue *wq) {
  unsigned long f = spin_lock_irqsave(&wq->lock);
  struct waiter *w;
  list_for_each_entry(w, &wq->head, link)
    if (try_to_wake_up(w->thread, TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) break;
  spin_unlock_irqrestore(&wq->lock, f);
}

bool wq_has_waiters(struct wait_queue *wq) { return !list_empty(&wq->head); }

void init_completion(struct completion *c) {
  wq_init(&c->wq);
  c->done = 0;
}

void complete(struct completion *c) {
  unsigned long f = spin_lock_irqsave(&c->wq.lock);
  c->done++;
  spin_unlock_irqrestore(&c->wq.lock, f);
  wake_up_one(&c->wq);
}

void complete_all(struct completion *c) {
  unsigned long f = spin_lock_irqsave(&c->wq.lock);
  c->done = 1U << 30;
  spin_unlock_irqrestore(&c->wq.lock, f);
  wake_up(&c->wq);
}

static bool try_consume(struct completion *c) {
  unsigned long f = spin_lock_irqsave(&c->wq.lock);
  bool ok = c->done > 0;
  if (ok && c->done < (1U << 30)) c->done--;
  spin_unlock_irqrestore(&c->wq.lock, f);
  return ok;
}

void wait_for_completion(struct completion *c) { wait_event(c->wq, try_consume(c)); }

long wait_for_completion_timeout(struct completion *c, long ns) {
  long r = 0;
  struct waiter w;
  u64 deadline = ktime_ns() + ns;
  for (;;) {
    prepare_to_wait(&c->wq, &w, TASK_UNINTERRUPTIBLE);
    if (try_consume(c)) {
      r = 1;
      break;
    }
    u64 now = ktime_ns();
    if (now >= deadline) break;
    schedule_timeout(deadline - now);
  }
  finish_wait(&c->wq, &w);
  return r;
}

void mutex_init(struct mutex *m) {
  m->owner = NULL;
  wq_init(&m->wq);
}

static bool mutex_try(struct mutex *m) {
  struct thread *expected = NULL;
  return __atomic_compare_exchange_n(&m->owner, &expected, current, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

void mutex_lock(struct mutex *m) { wait_event(m->wq, mutex_try(m)); }

int mutex_lock_interruptible(struct mutex *m) { return wait_event_interruptible(m->wq, mutex_try(m)); }

void mutex_unlock(struct mutex *m) {
  __atomic_store_n(&m->owner, NULL, __ATOMIC_RELEASE);
  wake_up_one(&m->wq);
}
