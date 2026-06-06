#include "dirent.h"

#include "string.h"
#include "syscall.h"

#define MAX_DIRS 4
static DIR dir_pool[MAX_DIRS];
static int dir_pool_used[MAX_DIRS];

static struct dirent static_dirent;

DIR* opendir(const char* name) {
  if (strcmp(name, "/") != 0 && strcmp(name, ".") != 0 &&
      strcmp(name, "") != 0) {
    return NULL;
  }

  int i;
  for (i = 0; i < MAX_DIRS; i++) {
    if (!dir_pool_used[i]) {
      dir_pool_used[i] = 1;

      struct {
        unsigned int sender;
        unsigned int cmd;
      } req;
      req.sender = sys_gettid();
      req.cmd = 1;  // List Dir

      sys_send(FS_SERVER_TID, &req, sizeof(req));

      struct {
        int size;
        char data[512];
      } reply;

      sys_recv(FS_SERVER_TID, &reply, sizeof(reply));

      if (reply.size < 0) {
        dir_pool_used[i] = 0;
        return NULL;
      }

      memcpy(dir_pool[i].buf, reply.data, reply.size);
      dir_pool[i].buf_size = reply.size;
      dir_pool[i].buf_offset = 0;

      return &dir_pool[i];
    }
  }
  return NULL;
}

struct dirent* readdir(DIR* dirp) {
  if (!dirp) return NULL;
  if (dirp->buf_offset >= dirp->buf_size) return NULL;

  int len = 0;
  while (dirp->buf_offset + len < dirp->buf_size) {
    char c = dirp->buf[dirp->buf_offset + len];
    if (c == '\n' || c == '\0') {
      break;
    }
    len++;
  }

  if (len == 0) {
    dirp->buf_offset++;
    return readdir(dirp);
  }

  memcpy(static_dirent.d_name, dirp->buf + dirp->buf_offset, len);
  static_dirent.d_name[len] = '\0';
  static_dirent.d_ino = 1;  // Stub

  dirp->buf_offset += len;
  if (dirp->buf_offset < dirp->buf_size &&
      dirp->buf[dirp->buf_offset] == '\n') {
    dirp->buf_offset++;
  }

  return &static_dirent;
}

int closedir(DIR* dirp) {
  if (!dirp) return -1;
  int i;
  for (i = 0; i < MAX_DIRS; i++) {
    if (&dir_pool[i] == dirp) {
      dir_pool_used[i] = 0;
      return 0;
    }
  }
  return -1;
}
