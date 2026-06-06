#ifndef _UNISTD_H_
#define _UNISTD_H_

typedef unsigned long size_t;
typedef long ssize_t;

int open(const char* pathname, int flags);
int close(int fd);
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);

#endif
