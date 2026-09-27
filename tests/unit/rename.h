/* Force-included when building kernel sources for host unit tests: give the
 * kernel's libc-like functions distinct names so they do not clash with the
 * host C library (and so tests can compare the two). */
#define memcpy k_memcpy
#define memmove k_memmove
#define memset k_memset
#define memcmp k_memcmp
#define memchr k_memchr
#define strlen k_strlen
#define strnlen k_strnlen
#define strcmp k_strcmp
#define strncmp k_strncmp
#define strchr k_strchr
#define strrchr k_strrchr
#define strlcpy k_strlcpy
#define strlcat k_strlcat
#define strsep k_strsep
#define strstr k_strstr
#define strtoul k_strtoul
#define strtol k_strtol
#define vsnprintf k_vsnprintf
#define snprintf k_snprintf
