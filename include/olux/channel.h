#ifndef OLUX_CHANNEL_H
#define OLUX_CHANNEL_H

#include <olux/types.h>

struct file;

int chan_create_pair(struct file **a, struct file **b);
bool is_channel(struct file *f);
int chan_send_kernel(struct file *end, const void *buf, size_t len);
/* Blocking receive (uninterruptible, timeout_ns < 0 = forever). */
ssize_t chan_recv_kernel(struct file *end, void *buf, size_t len, long timeout_ns, s32 *sender_pid);
bool chan_peer_closed(struct file *end);

#endif
