/*
 * OluxOS IPC channels (userspace ABI).
 *
 * A channel is a connected pair of endpoints, each an ordinary file
 * descriptor. Messages are datagrams of up to OLUX_MSG_MAX bytes and may
 * carry up to OLUX_MSG_MAX_FDS file descriptors (capabilities). The kernel
 * records the sender's pid/uid/gid in every message; receivers can rely on
 * that identity, it cannot be forged by the sender.
 *
 *   int olux_channel(int fds[2], int flags);              flags: O_CLOEXEC, O_NONBLOCK
 *   int olux_msg_send(int fd, const void *buf, size_t len, const int *fds, int nfds);
 *   ssize_t olux_msg_recv(int fd, void *buf, size_t len, struct olux_msg_info *info, int *fds);
 *
 * Endpoints support poll(): POLLIN (message queued), POLLOUT (room to send),
 * POLLHUP (peer closed). recv returns -EMSGSIZE (message kept) if the buffer
 * is too small; info->len then holds the required size.
 */
#ifndef UAPI_OLUX_IPC_H
#define UAPI_OLUX_IPC_H

#include <stdint.h>

#define OLUX_MSG_MAX (64 * 1024)
#define OLUX_MSG_MAX_FDS 8

#define SYS_olux_channel 1000
#define SYS_olux_msg_send 1001
#define SYS_olux_msg_recv 1002
#define SYS_olux_sysinfo 1003
#define SYS_olux_watchdog 1004

struct olux_msg_info {
  uint32_t len;        /* message length */
  uint32_t nfds;       /* descriptors received */
  int32_t sender_pid;  /* 0 = the kernel */
  uint32_t sender_uid;
  uint32_t sender_gid;
  uint32_t flags;
};

#endif
