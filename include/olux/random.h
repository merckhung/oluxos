#ifndef OLUX_RANDOM_H
#define OLUX_RANDOM_H

#include <olux/types.h>

/* ChaCha20-based CSPRNG, seeded from hardware RNGs and timing jitter. */
void random_init(void);
void add_entropy(const void *buf, size_t n, unsigned bits);
void get_random_bytes(void *buf, size_t n);
u64 get_random_u64(void);
bool random_is_seeded(void);

#endif
