/* libc shim for the in-kernel lwIP build */
#include <olux/string.h>
int lwip_atoi(const char *s);
#define atoi lwip_atoi
