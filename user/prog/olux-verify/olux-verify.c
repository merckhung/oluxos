/*
 * olux-verify - check an Ed25519 signature (RFC 8032), for signed update
 * bundles.
 *
 *   olux-verify PUBKEY FILE SIGNATURE
 *
 * PUBKEY is the 32-byte key: raw, 64 hex digits, or the DER/PEM
 * SubjectPublicKeyInfo that `openssl pkey -pubout` writes. SIGNATURE is 64
 * raw bytes or 128 hex digits (`openssl pkeyutl -sign -rawin`). Exit status
 * 0 = valid, 1 = invalid, 2 = usage or I/O error.
 *
 * Field and group arithmetic follow TweetNaCl (public domain); the curve
 * constants are derived at start-up rather than tabulated. Verification
 * handles only public data, so constant time is not needed.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint64_t u64;
typedef int64_t i64;
typedef i64 gf[16];

/* ---------------- SHA-512 (FIPS 180-4) ---------------- */

static const u64 K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};
static const u64 H0[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

struct sha512 {
  u64 h[8];
  u8 buf[128];
  size_t n;
  u64 total;
};

static u64 ror(u64 x, int n) { return x >> n | x << (64 - n); }

static void sha512_block(struct sha512 *s, const u8 *p) {
  u64 w[80], a[8];
  for (int i = 0; i < 16; i++) {
    w[i] = 0;
    for (int j = 0; j < 8; j++) w[i] = w[i] << 8 | p[8 * i + j];
  }
  for (int i = 16; i < 80; i++) {
    u64 s0 = ror(w[i - 15], 1) ^ ror(w[i - 15], 8) ^ (w[i - 15] >> 7);
    u64 s1 = ror(w[i - 2], 19) ^ ror(w[i - 2], 61) ^ (w[i - 2] >> 6);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  memcpy(a, s->h, sizeof(a));
  for (int i = 0; i < 80; i++) {
    u64 S1 = ror(a[4], 14) ^ ror(a[4], 18) ^ ror(a[4], 41);
    u64 ch = (a[4] & a[5]) ^ (~a[4] & a[6]);
    u64 t1 = a[7] + S1 + ch + K[i] + w[i];
    u64 S0 = ror(a[0], 28) ^ ror(a[0], 34) ^ ror(a[0], 39);
    u64 maj = (a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]);
    u64 t2 = S0 + maj;
    memmove(&a[1], &a[0], 7 * sizeof(u64));
    a[4] += t1;
    a[0] = t1 + t2;
  }
  for (int i = 0; i < 8; i++) s->h[i] += a[i];
}

static void sha512_init(struct sha512 *s) {
  memcpy(s->h, H0, sizeof(H0));
  s->n = 0;
  s->total = 0;
}

static void sha512_update(struct sha512 *s, const void *data, size_t len) {
  const u8 *p = data;
  s->total += len;
  while (len) {
    size_t k = 128 - s->n < len ? 128 - s->n : len;
    memcpy(s->buf + s->n, p, k);
    s->n += k;
    p += k;
    len -= k;
    if (s->n == 128) {
      sha512_block(s, s->buf);
      s->n = 0;
    }
  }
}

static void sha512_final(struct sha512 *s, u8 out[64]) {
  u64 bits = s->total * 8;
  u8 pad = 0x80, zero = 0, len[16] = {0};
  sha512_update(s, &pad, 1);
  while (s->n != 112) sha512_update(s, &zero, 1);
  for (int i = 0; i < 8; i++) len[15 - i] = (u8)(bits >> (8 * i));
  sha512_update(s, len, 16);
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++) out[8 * i + j] = (u8)(s->h[i] >> (56 - 8 * j));
}

/* ---------------- GF(2^255 - 19), 16 limbs of 16 bits ---------------- */

static const gf gf0 = {0}, gf1 = {1};
static gf D, D2, I; /* curve constant d, 2d, sqrt(-1) */
static gf BX, BY;   /* base point */

static void set25519(gf r, const gf a) {
  for (int i = 0; i < 16; i++) r[i] = a[i];
}

static void car25519(gf o) {
  for (int i = 0; i < 16; i++) {
    o[i] += (i64)1 << 16;
    i64 c = o[i] >> 16;
    o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
    o[i] -= c << 16;
  }
}

static void sel25519(gf p, gf q, int b) {
  i64 c = ~(i64)(b - 1);
  for (int i = 0; i < 16; i++) {
    i64 t = c & (p[i] ^ q[i]);
    p[i] ^= t;
    q[i] ^= t;
  }
}

static void pack25519(u8 *o, const gf n) {
  gf m, t;
  set25519(t, n);
  car25519(t);
  car25519(t);
  car25519(t);
  for (int j = 0; j < 2; j++) {
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; i++) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    int b = (int)((m[15] >> 16) & 1);
    m[14] &= 0xffff;
    sel25519(t, m, 1 - b);
  }
  for (int i = 0; i < 16; i++) {
    o[2 * i] = (u8)(t[i] & 0xff);
    o[2 * i + 1] = (u8)(t[i] >> 8);
  }
}

static int neq25519(const gf a, const gf b) {
  u8 c[32], d[32];
  pack25519(c, a);
  pack25519(d, b);
  return memcmp(c, d, 32) != 0;
}

static u8 par25519(const gf a) {
  u8 d[32];
  pack25519(d, a);
  return d[0] & 1;
}

static void unpack25519(gf o, const u8 *n) {
  for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((i64)n[2 * i + 1] << 8);
  o[15] &= 0x7fff;
}

static void A(gf o, const gf a, const gf b) {
  for (int i = 0; i < 16; i++) o[i] = a[i] + b[i];
}

static void Z(gf o, const gf a, const gf b) {
  for (int i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

static void M(gf o, const gf a, const gf b) {
  i64 t[31] = {0};
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
  for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
  for (int i = 0; i < 16; i++) o[i] = t[i];
  car25519(o);
  car25519(o);
}

static void S(gf o, const gf a) { M(o, a, a); }

static void inv25519(gf o, const gf i) { /* i^(p-2) */
  gf c;
  set25519(c, i);
  for (int a = 253; a >= 0; a--) {
    S(c, c);
    if (a != 2 && a != 4) M(c, c, i);
  }
  set25519(o, c);
}

static void pow2523(gf o, const gf i) { /* i^((p-5)/8) */
  gf c;
  set25519(c, i);
  for (int a = 250; a >= 0; a--) {
    S(c, c);
    if (a != 1) M(c, c, i);
  }
  set25519(o, c);
}

/* ---------------- the Edwards group, extended coordinates ---------------- */

static void add(gf p[4], gf q[4]) {
  gf a, b, c, d, t, e, f, g, h;
  Z(a, p[1], p[0]);
  Z(t, q[1], q[0]);
  M(a, a, t);
  A(b, p[0], p[1]);
  A(t, q[0], q[1]);
  M(b, b, t);
  M(c, p[3], q[3]);
  M(c, c, D2);
  M(d, p[2], q[2]);
  A(d, d, d);
  Z(e, b, a);
  Z(f, d, c);
  A(g, d, c);
  A(h, b, a);
  M(p[0], e, f);
  M(p[1], h, g);
  M(p[2], g, f);
  M(p[3], e, h);
}

static void cswap(gf p[4], gf q[4], u8 b) {
  for (int i = 0; i < 4; i++) sel25519(p[i], q[i], b);
}

static void pack(u8 *r, gf p[4]) {
  gf tx, ty, zi;
  inv25519(zi, p[2]);
  M(tx, p[0], zi);
  M(ty, p[1], zi);
  pack25519(r, ty);
  r[31] ^= (u8)(par25519(tx) << 7);
}

static void scalarmult(gf p[4], gf q[4], const u8 *s) {
  set25519(p[0], gf0);
  set25519(p[1], gf1);
  set25519(p[2], gf1);
  set25519(p[3], gf0);
  for (int i = 255; i >= 0; --i) {
    u8 b = (s[i / 8] >> (i & 7)) & 1;
    cswap(p, q, b);
    add(q, p);
    add(p, p);
    cswap(p, q, b);
  }
}

static void scalarbase(gf p[4], const u8 *s) {
  gf q[4];
  set25519(q[0], BX);
  set25519(q[1], BY);
  set25519(q[2], gf1);
  M(q[3], BX, BY);
  scalarmult(p, q, s);
}

/* Decode a point and negate it; -1 if it is not on the curve. */
static int unpackneg(gf r[4], const u8 p[32]) {
  gf t, chk, num, den, den2, den4, den6;
  set25519(r[2], gf1);
  unpack25519(r[1], p);
  S(num, r[1]);
  M(den, num, D);
  Z(num, num, r[2]);
  A(den, r[2], den);
  S(den2, den);
  S(den4, den2);
  M(den6, den4, den2);
  M(t, den6, num);
  M(t, t, den);
  pow2523(t, t);
  M(t, t, num);
  M(t, t, den);
  M(t, t, den);
  M(r[0], t, den);
  S(chk, r[0]);
  M(chk, chk, den);
  if (neq25519(chk, num)) M(r[0], r[0], I);
  S(chk, r[0]);
  M(chk, chk, den);
  if (neq25519(chk, num)) return -1;
  if (par25519(r[0]) == (p[31] >> 7)) Z(r[0], gf0, r[0]);
  M(r[3], r[0], r[1]);
  return 0;
}

/* d = -121665/121666, 2d, sqrt(-1) = 2^((p-1)/4), and B from its encoding
 * (y = 4/5, x even). */
static void curve_init(void) {
  gf n = {0xdb41, 1}, m = {0xdb42, 1}, two = {2}, four = {4}, five = {5}, t;
  inv25519(t, m);
  M(t, n, t);
  Z(D, gf0, t);
  A(D2, D, D);
  set25519(I, gf1);
  for (int bit = 252; bit >= 0; bit--) { /* (p-1)/4 = 2^253 - 5 */
    S(I, I);
    if (bit != 2) M(I, I, two);
  }
  inv25519(t, five);
  M(BY, four, t);
  u8 enc[32];
  pack25519(enc, BY);
  gf r[4];
  unpackneg(r, enc); /* x of -B; B's x is its negation */
  Z(BX, gf0, r[0]);
}

/* ---------------- scalars mod L ---------------- */

static const i64 L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                          0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                          0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

static void modL(u8 *r, i64 x[64]) {
  i64 carry;
  int i, j;
  for (i = 63; i >= 32; --i) {
    carry = 0;
    for (j = i - 32; j < i - 12; ++j) {
      x[j] += carry - 16 * x[i] * L[j - (i - 32)];
      carry = (x[j] + 128) >> 8;
      x[j] -= carry * 256;
    }
    x[j] += carry;
    x[i] = 0;
  }
  carry = 0;
  for (j = 0; j < 32; j++) {
    x[j] += carry - (x[31] >> 4) * L[j];
    carry = x[j] >> 8;
    x[j] &= 255;
  }
  for (j = 0; j < 32; j++) x[j] -= carry * L[j];
  for (i = 0; i < 32; i++) {
    x[i + 1] += x[i] >> 8;
    r[i] = (u8)(x[i] & 255);
  }
}

static void reduce(u8 *r) {
  i64 x[64];
  for (int i = 0; i < 64; i++) x[i] = r[i];
  memset(r, 0, 64);
  modL(r, x);
}

/* s < L, as RFC 8032 requires (no malleable signatures) */
static int scalar_canonical(const u8 *s) {
  for (int i = 31; i >= 0; i--) {
    if (s[i] < L[i]) return 1;
    if (s[i] > L[i]) return 0;
  }
  return 0;
}

static int ed25519_verify(const u8 sig[64], const u8 *msg, size_t len, const u8 pk[32]) {
  gf p[4], q[4];
  u8 h[64], t[32];
  if (!scalar_canonical(sig + 32) || unpackneg(q, pk)) return -1;
  struct sha512 s;
  sha512_init(&s);
  sha512_update(&s, sig, 32);
  sha512_update(&s, pk, 32);
  sha512_update(&s, msg, len);
  sha512_final(&s, h);
  reduce(h);
  scalarmult(p, q, h); /* -hA */
  scalarbase(q, sig + 32);
  add(p, q); /* sB - hA, which must be R */
  pack(t, p);
  return memcmp(sig, t, 32) ? -1 : 0;
}

/* ---------------- files ---------------- */

static u8 *slurp(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror(path);
    return NULL;
  }
  size_t cap = 65536, n = 0;
  u8 *b = malloc(cap);
  for (size_t k; b && (k = fread(b + n, 1, cap - n, f)) > 0;) {
    n += k;
    if (n == cap) b = realloc(b, cap *= 2);
  }
  fclose(f);
  if (!b) fprintf(stderr, "olux-verify: out of memory\n");
  *len = n;
  return b;
}

static int hexval(int c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Exactly `want` bytes as hex text (whitespace ignored); 0 on success. */
static int from_hex(const u8 *in, size_t len, u8 *out, size_t want) {
  size_t n = 0;
  int hi = -1;
  for (size_t i = 0; i < len; i++) {
    if (in[i] == ' ' || in[i] == '\n' || in[i] == '\r' || in[i] == '\t') continue;
    int v = hexval(in[i]);
    if (v < 0 || n >= want) return -1;
    if (hi < 0) {
      hi = v;
    } else {
      out[n++] = (u8)(hi << 4 | v);
      hi = -1;
    }
  }
  return n == want && hi < 0 ? 0 : -1;
}

static int b64val(int c) {
  return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
         : c == '+' ? 62 : c == '/' ? 63 : -1;
}

/* The 44-byte DER of an Ed25519 SubjectPublicKeyInfo ends in the key. */
static const u8 spki_prefix[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};

static int load_key(const u8 *in, size_t len, u8 pk[32]) {
  if (len == 32) {
    memcpy(pk, in, 32);
    return 0;
  }
  if (len == 44 && !memcmp(in, spki_prefix, 12)) {
    memcpy(pk, in + 12, 32);
    return 0;
  }
  const char *begin = "-----BEGIN PUBLIC KEY-----";
  if (len > strlen(begin) && !memcmp(in, begin, strlen(begin))) {
    u8 der[64];
    size_t n = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = strlen(begin); i < len && in[i] != '-'; i++) {
      int v = b64val(in[i]);
      if (v < 0) continue;
      acc = acc << 6 | (unsigned)v;
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        if (n < sizeof(der)) der[n++] = (u8)(acc >> bits);
      }
    }
    return load_key(der, n, pk);
  }
  return from_hex(in, len, pk, 32);
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: olux-verify PUBKEY FILE SIGNATURE\n");
    return 2;
  }
  size_t klen, mlen, slen;
  u8 *k = slurp(argv[1], &klen), *m = slurp(argv[2], &mlen), *s = slurp(argv[3], &slen);
  if (!k || !m || !s) return 2;
  u8 pk[32], sig[64];
  if (load_key(k, klen, pk)) {
    fprintf(stderr, "olux-verify: %s: not an Ed25519 public key\n", argv[1]);
    return 2;
  }
  if (slen == 64)
    memcpy(sig, s, 64);
  else if (from_hex(s, slen, sig, 64)) {
    fprintf(stderr, "olux-verify: %s: not an Ed25519 signature\n", argv[3]);
    return 2;
  }
  curve_init();
  if (ed25519_verify(sig, m, mlen, pk)) {
    fprintf(stderr, "olux-verify: %s: BAD signature\n", argv[2]);
    return 1;
  }
  printf("olux-verify: %s: signature OK\n", argv[2]);
  return 0;
}
