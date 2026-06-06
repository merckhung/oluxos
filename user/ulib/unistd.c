#include "unistd.h"

#include "string.h"
#include "syscall.h"

#define FD_TYPE_UNUSED 0
#define FD_TYPE_UART 1
#define FD_TYPE_FILE 2

typedef struct {
  int type;
  int srv_tid;
  unsigned int handle;
  unsigned int offset;
} FdEntry;

#define MAX_FD 16

static FdEntry fd_table[MAX_FD];
static int fd_table_initialized = 0;

static void init_fd_table(void) {
  int i;
  for (i = 0; i < MAX_FD; i++) {
    fd_table[i].type = FD_TYPE_UNUSED;
  }
  // FD 0, 1, 2 map to UART Driver
  fd_table[0].type = FD_TYPE_UART;
  fd_table[0].srv_tid = UART_DRIVER_TID;

  fd_table[1].type = FD_TYPE_UART;
  fd_table[1].srv_tid = UART_DRIVER_TID;

  fd_table[2].type = FD_TYPE_UART;
  fd_table[2].srv_tid = UART_DRIVER_TID;

  fd_table_initialized = 1;
}

static FdEntry* get_fd(int fd) {
  if (!fd_table_initialized) {
    init_fd_table();
  }
  if (fd < 0 || fd >= MAX_FD) return NULL;
  if (fd_table[fd].type == FD_TYPE_UNUSED) return NULL;
  return &fd_table[fd];
}

static void format_filename(const char* src, char* dest) {
  memset(dest, ' ', 11);
  int i = 0;
  int d = 0;
  while (src[i] && src[i] != '.' && d < 8) {
    char c = src[i];
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    dest[d++] = c;
    i++;
  }
  while (src[i] && src[i] != '.') i++;
  if (src[i] == '.') i++;
  d = 8;
  while (src[i] && d < 11) {
    char c = src[i];
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    dest[d++] = c;
    i++;
  }
}

int open(const char* pathname, int flags) {
  if (!fd_table_initialized) {
    init_fd_table();
  }

  int fd;
  for (fd = 3; fd < MAX_FD; fd++) {
    if (fd_table[fd].type == FD_TYPE_UNUSED) {
      break;
    }
  }
  if (fd == MAX_FD) return -1;

  char formatted[11];
  format_filename(pathname, formatted);

  struct {
    unsigned int sender;
    unsigned int cmd;
    char filename[11];
  } req;
  req.sender = sys_gettid();
  req.cmd = 2;  // Open File
  memcpy(req.filename, formatted, 11);

  sys_send(FS_SERVER_TID, &req, sizeof(req));

  struct {
    int size;
  } reply;
  sys_recv(FS_SERVER_TID, &reply, sizeof(reply));

  if (reply.size < 0) {
    return -1;
  }

  fd_table[fd].type = FD_TYPE_FILE;
  fd_table[fd].srv_tid = FS_SERVER_TID;
  fd_table[fd].handle = reply.size;
  fd_table[fd].offset = 0;

  return fd;
}

int close(int fd) {
  FdEntry* f = get_fd(fd);
  if (!f) return -1;

  if (f->type == FD_TYPE_FILE) {
    struct {
      unsigned int sender;
      unsigned int cmd;
      unsigned int handle;
    } req;
    req.sender = sys_gettid();
    req.cmd = 4;  // Close File
    req.handle = f->handle;

    sys_send(f->srv_tid, &req, sizeof(req));

    struct {
      int status;
    } reply;
    sys_recv(f->srv_tid, &reply, sizeof(reply));
  }

  f->type = FD_TYPE_UNUSED;
  return 0;
}

ssize_t write(int fd, const void* buf, size_t count) {
  FdEntry* f = get_fd(fd);
  if (!f) return -1;

  if (f->type == FD_TYPE_UART) {
    size_t written = 0;
    while (written < count) {
      struct {
        unsigned int sender;
        unsigned int cmd;
        unsigned int size;
        char data[64];
      } req;
      req.sender = sys_gettid();
      req.cmd = 0;  // Write

      size_t chunk = count - written;
      if (chunk > 63) chunk = 63;

      memcpy(req.data, (const char*)buf + written, chunk);
      req.data[chunk] = '\0';
      req.size = chunk;

      sys_send(f->srv_tid, &req, 12 + chunk + 1);
      written += chunk;
    }
    return written;
  }
  return -1;
}

ssize_t read(int fd, void* buf, size_t count) {
  FdEntry* f = get_fd(fd);
  if (!f) return -1;

  if (f->type == FD_TYPE_UART) {
    size_t read_bytes = 0;
    char* ptr = buf;
    while (read_bytes < count) {
      struct {
        unsigned int sender;
        unsigned int cmd;
      } req;
      req.sender = sys_gettid();
      req.cmd = 1;  // Read

      sys_send(f->srv_tid, &req, 8);
      char c;
      sys_recv(f->srv_tid, &c, 1);

      ptr[read_bytes++] = c;

      if (c == '\n' || c == '\r') {
        break;
      }
    }
    return read_bytes;
  } else if (f->type == FD_TYPE_FILE) {
    struct {
      unsigned int sender;
      unsigned int cmd;
      unsigned int handle;
      unsigned int count;
    } req;
    req.sender = sys_gettid();
    req.cmd = 3;  // Read File Handle
    req.handle = f->handle;
    req.count = count > 512 ? 512 : count;

    sys_send(f->srv_tid, &req, sizeof(req));

    struct {
      int size;
      char data[512];
    } reply;
    sys_recv(f->srv_tid, &reply, sizeof(reply));

    if (reply.size < 0) {
      return -1;
    }

    memcpy(buf, reply.data, reply.size);
    f->offset += reply.size;
    return reply.size;
  }
  return -1;
}
