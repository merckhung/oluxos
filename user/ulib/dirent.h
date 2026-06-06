#ifndef _DIRENT_H_
#define _DIRENT_H_

typedef struct {
    char buf[512];
    int buf_size;
    int buf_offset;
} DIR;

struct dirent {
    unsigned long d_ino;
    char d_name[256];
};

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

#endif
