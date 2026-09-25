#ifndef OLUX_LIST_H
#define OLUX_LIST_H

#include <olux/compiler.h>
#include <olux/types.h>

/* Intrusive circular doubly-linked list. */
struct list_head {
  struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) {&(name), &(name)}
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void list_init(struct list_head *l) { l->next = l->prev = l; }
static inline bool list_empty(const struct list_head *l) { return l->next == l; }

static inline void __list_add(struct list_head *n, struct list_head *prev,
                              struct list_head *next) {
  next->prev = n;
  n->next = next;
  n->prev = prev;
  prev->next = n;
}

static inline void list_add(struct list_head *n, struct list_head *head) {
  __list_add(n, head, head->next);
}

static inline void list_add_tail(struct list_head *n, struct list_head *head) {
  __list_add(n, head->prev, head);
}

static inline void list_del(struct list_head *e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
  e->next = e->prev = e;
}

static inline bool list_linked(const struct list_head *e) { return e->next != e; }

#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(head, type, member) list_entry((head)->next, type, member)

#define list_for_each(pos, head) \
  for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head) \
  for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

#define list_for_each_entry(pos, head, member)                   \
  for (pos = list_entry((head)->next, __typeof__(*pos), member); \
       &pos->member != (head);                                   \
       pos = list_entry(pos->member.next, __typeof__(*pos), member))

#define list_for_each_entry_safe(pos, n, head, member)            \
  for (pos = list_entry((head)->next, __typeof__(*pos), member),  \
      n = list_entry(pos->member.next, __typeof__(*pos), member); \
       &pos->member != (head);                                    \
       pos = n, n = list_entry(n->member.next, __typeof__(*n), member))

#endif
