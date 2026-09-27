/* Userspace wrappers for OluxOS IPC channels (see include/uapi/olux/ipc.h). */
#ifndef OLUX_USER_IPC_H
#define OLUX_USER_IPC_H

#include <olux/ipc.h>
#include <stddef.h>
#include <sys/types.h>

int olux_channel(int fds[2], int flags);
int olux_msg_send(int fd, const void *buf, size_t len, const int *fds, int nfds);
ssize_t olux_msg_recv(int fd, void *buf, size_t len, struct olux_msg_info *info, int *fds);

#endif
