#ifndef _STRING_H_
#define _STRING_H_

typedef unsigned long size_t;
#define NULL ((void*)0)

void* memcpy(void *dest, const void *src, size_t n);
void* memset(void *dest, int val, size_t n);
int strcmp(const char *s1, const char *s2);
size_t strlen(const char *s);
char* strstr(const char *haystack, const char *needle);

#endif
