#include "unistd.h"

#include "stdio.h"
#include "dirent.h"
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

  char resolved[64];
  if (resolve_absolute_path(pathname, resolved, 64) < 0) {
    return -1;
  }

  struct {
    unsigned int sender;
    unsigned int cmd;
    char filename[64];
  } req;
  req.sender = sys_gettid();
  req.cmd = 2;  // Open File
  
  int len = strlen(resolved);
  if (len > 63) len = 63;
  memcpy(req.filename, resolved, len);
  req.filename[len] = '\0';

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

static char current_working_directory[128] = "/";

char* getcwd(char* buf, size_t size) {
  if (!buf || size == 0) return NULL;
  int len = strlen(current_working_directory);
  if (len >= size) return NULL;
  memcpy(buf, current_working_directory, len + 1);
  return buf;
}

int resolve_absolute_path(const char* path, char* out_buf, size_t max_len) {
  char temp_path[128];
  if (path[0] == '/') {
    strncpy(temp_path, path, 128);
  } else {
    int cwd_len = strlen(current_working_directory);
    memcpy(temp_path, current_working_directory, cwd_len);
    if (current_working_directory[cwd_len - 1] != '/') {
      temp_path[cwd_len] = '/';
      cwd_len++;
    }
    int path_len = strlen(path);
    if (cwd_len + path_len >= 128) return -1;
    memcpy(temp_path + cwd_len, path, path_len + 1);
  }

  char norm_path[128] = "/";
  int norm_len = 1;

  const char* p = temp_path;
  if (*p == '/') p++;

  while (*p) {
    char component[32];
    int comp_len = 0;
    while (*p && *p != '/') {
      if (comp_len < 31) component[comp_len++] = *p;
      p++;
    }
    component[comp_len] = '\0';
    if (*p == '/') p++;

    if (comp_len == 0 || strcmp(component, ".") == 0) {
      continue;
    }
    if (strcmp(component, "..") == 0) {
      if (norm_len > 1) {
        norm_len--;
        while (norm_len > 0 && norm_path[norm_len - 1] != '/') {
          norm_len--;
        }
        if (norm_len == 0) norm_len = 1;
        norm_path[norm_len] = '\0';
      }
    } else {
      int n_cwd_len = strlen(norm_path);
      if (n_cwd_len > 1 && norm_path[n_cwd_len - 1] != '/') {
        norm_path[n_cwd_len++] = '/';
      }
      if (n_cwd_len + comp_len >= 128) return -1;
      memcpy(norm_path + n_cwd_len, component, comp_len + 1);
      norm_len = strlen(norm_path);
    }
  }

  int final_len = strlen(norm_path);
  if (final_len >= max_len) return -1;
  memcpy(out_buf, norm_path, final_len + 1);
  return 0;
}

int chdir(const char* path) {
  char resolved_path[128];
  if (resolve_absolute_path(path, resolved_path, 128) < 0) {
    printf("chdir: resolve_absolute_path failed!\n");
    return -1;
  }
  DIR* d = opendir(resolved_path);
  if (!d) return -1;
  closedir(d);
  strncpy(current_working_directory, resolved_path, 128);
  return 0;
}
