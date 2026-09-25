/*
 * Kernel CSPRNG: a ChaCha20 keystream generator whose 256-bit key is mixed
 * with every entropy contribution (hardware RNG output, interrupt timing)
 * and re-keyed after each request for forward secrecy.
 * Exposes /dev/random, /dev/urandom and getrandom(2).
 */
#include <olux/device.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/random.h>
#include <olux/sched.h>
#include <olux/spinlock.h>
#include <olux/time.h>
#include <olux/uaccess.h>

static u32 key[8];
static u64 counter;
static unsigned entropy_bits;
static DEFINE_SPINLOCK(rng_lock);
static DEFINE_WAIT_QUEUE(rng_wait);

#define ROTL(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define QR(a, b, c, d) \
  (a += b, d ^= a, d = ROTL(d, 16), c += d, b ^= c, b = ROTL(b, 12), a += b, d ^= a, d = ROTL(d, 8), c += d, b ^= c, b = ROTL(b, 7))

static void chacha20_block(const u32 k[8], u64 ctr, u32 nonce, u32 out[16]) {
  u32 in[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7],
                (u32)ctr, (u32)(ctr >> 32), nonce, 0};
  u32 x[16];
  memcpy(x, in, sizeof(x));
  for (int i = 0; i < 10; i++) {
    QR(x[0], x[4], x[8], x[12]);
    QR(x[1], x[5], x[9], x[13]);
    QR(x[2], x[6], x[10], x[14]);
    QR(x[3], x[7], x[11], x[15]);
    QR(x[0], x[5], x[10], x[15]);
    QR(x[1], x[6], x[11], x[12]);
    QR(x[2], x[7], x[8], x[13]);
    QR(x[3], x[4], x[9], x[14]);
  }
  for (int i = 0; i < 16; i++) out[i] = x[i] + in[i];
}

/* Mix input into the key: key = ChaCha20(key XOR H(input)) truncated. */
void add_entropy(const void *buf, size_t n, unsigned bits) {
  unsigned long f = spin_lock_irqsave(&rng_lock);
  const u8 *p = buf;
  u32 block[16];
  for (size_t off = 0; off < n || off == 0; off += 32) {
    for (size_t i = 0; i < 32 && off + i < n; i++) ((u8 *)key)[i] ^= p[off + i];
    chacha20_block(key, counter++, 0x6d697865, block);
    memcpy(key, block, sizeof(key));
    if (n == 0) break;
  }
  bool was = entropy_bits >= 256;
  entropy_bits = MIN(entropy_bits + bits, 4096u);
  spin_unlock_irqrestore(&rng_lock, f);
  if (!was && entropy_bits >= 256) wake_up(&rng_wait);
}

bool random_is_seeded(void) { return entropy_bits >= 256; }

void get_random_bytes(void *buf, size_t n) {
  u8 *out = buf;
  u32 block[16];
  unsigned long f = spin_lock_irqsave(&rng_lock);
  while (n) {
    chacha20_block(key, counter++, 0, block);
    size_t c = MIN(n, sizeof(block));
    memcpy(out, block, c);
    out += c;
    n -= c;
  }
  /* fast key erasure */
  chacha20_block(key, counter++, 1, block);
  memcpy(key, block, sizeof(key));
  spin_unlock_irqrestore(&rng_lock, f);
  memset(block, 0, sizeof(block));
}

u64 get_random_u64(void) {
  u64 v;
  get_random_bytes(&v, sizeof(v));
  return v;
}

void random_init(void) {
  /* Initial seed: counter jitter, identifiers. Hardware RNG drivers
   * contribute real entropy once probed. */
  u64 seed[8];
  for (int i = 0; i < 8; i++) {
    seed[i] = read_sysreg(cntvct_el0) ^ (read_sysreg(cntvct_el0) << 17);
    for (volatile int j = 0; j < 97 * (i + 1); j++)
      ;
  }
  seed[0] ^= read_sysreg(midr_el1);
  seed[1] ^= read_sysreg(mpidr_el1);
  add_entropy(seed, sizeof(seed), 0);
}

/* ---- character devices ---- */

static ssize_t rnd_read(struct file *f, struct iobuf *b, loff_t *pos) {
  u8 tmp[256];
  size_t done = 0;
  while (done < b->len) {
    size_t c = MIN(b->len - done, sizeof(tmp));
    get_random_bytes(tmp, c);
    if (iob_write(b, done, tmp, c)) return done ? (ssize_t)done : -EFAULT;
    done += c;
    if (done >= 1 << 20) break;
  }
  return done;
}

static ssize_t rnd_write(struct file *f, struct iobuf *b, loff_t *pos) {
  u8 tmp[256];
  size_t done = 0;
  while (done < b->len) {
    size_t c = MIN(b->len - done, sizeof(tmp));
    if (iob_read(b, done, tmp, c)) return -EFAULT;
    add_entropy(tmp, c, 0); /* user data is mixed but not credited */
    done += c;
  }
  return done;
}

static unsigned rnd_poll(struct file *f, struct poll_table *pt) { return POLLIN | POLLOUT; }

static const struct file_operations random_fops = {.read = rnd_read, .write = rnd_write, .poll = rnd_poll};

long sys_getrandom(u64 buf, u64 len, u64 flags);
long sys_getrandom(u64 buf, u64 len, u64 flags) {
  if (flags & ~7UL) return -EINVAL;
  if (!random_is_seeded() && !(flags & 2 /* GRND_INSECURE */)) {
    if (flags & 1 /* GRND_NONBLOCK */) return -EAGAIN;
    int r = wait_event_interruptible_timeout(rng_wait, random_is_seeded(), (long)(2 * NSEC_PER_SEC));
    if (r == -ERESTARTSYS) return -EINTR;
    /* After 2 s continue with the jitter-seeded state rather than hang
     * boot on boards without a hardware RNG driver. */
    if (!random_is_seeded()) {
      pr_warn("random: crng not fully seeded; using jitter entropy\n");
      add_entropy(NULL, 0, 256);
    }
  }
  struct iobuf b = ubuf(buf, MIN(len, (u64)(32 << 20)));
  return rnd_read(NULL, &b, NULL);
}

static int random_devices(void) {
  register_chrdev(MKDEV(1, 8), "random", &random_fops, NULL);
  register_chrdev(MKDEV(1, 9), "urandom", &random_fops, NULL);
  devfs_create("random", S_IFCHR | 0666, MKDEV(1, 8));
  devfs_create("urandom", S_IFCHR | 0666, MKDEV(1, 9));
  return 0;
}
core_initcall(random_devices);
