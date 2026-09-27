#include <olux_ipc.h>
#include <unistd.h>

int olux_channel(int fds[2], int flags) { return (int)syscall(SYS_olux_channel, fds, flags); }

int olux_msg_send(int fd, const void *buf, size_t len, const int *fds, int nfds) {
  return (int)syscall(SYS_olux_msg_send, fd, buf, len, fds, nfds);
}

ssize_t olux_msg_recv(int fd, void *buf, size_t len, struct olux_msg_info *info, int *fds) {
  return syscall(SYS_olux_msg_recv, fd, buf, len, info, fds);
}
