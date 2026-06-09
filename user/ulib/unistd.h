#ifndef _UNISTD_H_
#define _UNISTD_H_

#include <stddef.h>
typedef long ssize_t;

int open(const char* pathname, int flags);
int close(int fd);
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int chdir(const char* path);
char* getcwd(char* buf, size_t size);
int resolve_absolute_path(const char* path, char* out_buf, size_t max_len);

#endif
