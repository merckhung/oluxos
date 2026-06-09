#ifndef _STRING_H_
#define _STRING_H_

#include <stddef.h>
#ifndef NULL
#define NULL ((void*)0)
#endif

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int val, size_t n);
int strcmp(const char* s1, const char* s2);
size_t strlen(const char* s);
char* strstr(const char* haystack, const char* needle);
int memcmp(const void* s1, const void* s2, size_t n);
char* strncpy(char* dest, const char* src, size_t n);

#endif
