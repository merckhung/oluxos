/* TEMPORARY: socket syscalls until the network stack lands. */
#include <olux/kernel.h>

#define STUB(name) \
  long sys_##name(void); \
  long sys_##name(void) { return -EAFNOSUPPORT; }
STUB(socket)
STUB(socketpair)
STUB(bind)
STUB(listen)
STUB(accept)
STUB(accept4)
STUB(connect)
STUB(getsockname)
STUB(getpeername)
STUB(sendto)
STUB(recvfrom)
STUB(setsockopt)
STUB(getsockopt)
STUB(shutdown)
STUB(sendmsg)
STUB(recvmsg)
