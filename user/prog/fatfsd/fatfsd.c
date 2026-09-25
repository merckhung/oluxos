/*
 * fatfsd - FAT12/16/32 filesystem server (read/write, long file names).
 *
 *   fatfsd [-r] <device> <mountpoint>
 *
 * Runs as a userfs server: the kernel forwards VFS operations over an IPC
 * channel. A crash of this process never takes the kernel down; init
 * restarts it and it re-attaches to the existing mount.
 *
 * Inode numbers (must survive a server restart):
 *   1                        root directory
 *   start cluster            files/directories that own clusters
 *   POS_FLAG|dclus<<16|idx   empty files: location of the short entry
 * A table maps numbers to directory-entry locations; cluster numbers not in
 * the table (e.g. after a restart) are found by scanning the tree.
 *
 * Robustness: every cluster number read from disk is range-checked, every
 * chain walk is bounded, and a volume that was not cleanly unmounted gets a
 * repair pass (lost chains freed, files trimmed to their chains).
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <ufs_server.h>
#include <unistd.h>

#define ROOT_INO 1ULL
#define POS_FLAG (1ULL << 62)
#define MAX_DIR_ENTRIES 65536
#define MAX_FILE 0xFFFFFFFFULL

#define A_RO 0x01
#define A_HIDDEN 0x02
#define A_SYSTEM 0x04
#define A_VOLUME 0x08
#define A_DIR 0x10
#define A_ARCH 0x20
#define A_LFN 0x0F

#define DT_DIR_ 4
#define DT_REG_ 8

struct de {
  uint8_t name[11];
  uint8_t attr, ntres, ctime_tenth;
  uint16_t ctime, cdate, adate, clus_hi, mtime, mdate, clus_lo;
  uint32_t size;
}; /* naturally aligned: no packing needed */
_Static_assert(sizeof(struct de) == 32, "dirent size");

static const uint8_t lfn_off[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

static struct {
  int fd;
  bool ro;
  int type; /* 12, 16, 32 */
  uint32_t bps, cs, nfats, fatsz, root_ents, root_bytes, nclus, root_clus, fsinfo_sec;
  uint64_t fat_off, root_off, data_off;
  uint8_t *fat;
  uint8_t *fat_dirty; /* per FAT sector */
  bool fat_changed;
  uint32_t free_count, next_free;
  uint8_t *zero;
  const char *dev;
} F;

/* ---------------- device I/O ---------------- */

static int dev_read(uint64_t off, void *buf, size_t len) {
  ssize_t n = pread(F.fd, buf, len, (off_t)off);
  return n == (ssize_t)len ? 0 : -EIO;
}

static int dev_write(uint64_t off, const void *buf, size_t len) {
  if (F.ro) return -EROFS;
  ssize_t n = pwrite(F.fd, buf, len, (off_t)off);
  return n == (ssize_t)len ? 0 : (n < 0 && errno == ENOSPC ? -ENOSPC : -EIO);
}

static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---------------- FAT ---------------- */

static bool valid(uint32_t c) { return c >= 2 && c < F.nclus + 2; }
static uint32_t eoc_min(void) { return F.type == 32 ? 0x0FFFFFF8 : F.type == 16 ? 0xFFF8 : 0xFF8; }
static uint32_t eoc(void) { return F.type == 32 ? 0x0FFFFFFF : F.type == 16 ? 0xFFFF : 0xFFF; }
static uint32_t bad(void) { return F.type == 32 ? 0x0FFFFFF7 : F.type == 16 ? 0xFFF7 : 0xFF7; }

static uint32_t fat_get(uint32_t c) {
  switch (F.type) {
    case 32:
      return le32(F.fat + c * 4) & 0x0FFFFFFF;
    case 16:
      return le16(F.fat + c * 2);
    default: {
      uint32_t o = c + c / 2;
      uint16_t v = le16(F.fat + o);
      return c & 1 ? v >> 4 : v & 0xFFF;
    }
  }
}

static void mark_dirty(uint32_t o, uint32_t len) {
  for (uint32_t s = o / F.bps; s <= (o + len - 1) / F.bps; s++) F.fat_dirty[s] = 1;
  F.fat_changed = true;
}

static void fat_set(uint32_t c, uint32_t v) {
  uint8_t *p;
  switch (F.type) {
    case 32:
      p = F.fat + c * 4;
      v = (v & 0x0FFFFFFF) | (le32(p) & 0xF0000000);
      p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24;
      mark_dirty(c * 4, 4);
      break;
    case 16:
      p = F.fat + c * 2;
      p[0] = v, p[1] = v >> 8;
      mark_dirty(c * 2, 2);
      break;
    default: {
      uint32_t o = c + c / 2;
      uint16_t w = le16(F.fat + o);
      w = c & 1 ? (w & 0x000F) | (v & 0xFFF) << 4 : (w & 0xF000) | (v & 0xFFF);
      F.fat[o] = w, F.fat[o + 1] = w >> 8;
      mark_dirty(o, 2);
    }
  }
}

/* Next cluster of a chain, or 0 at the end (or on a corrupt link). */
static uint32_t next(uint32_t c) {
  uint32_t v = fat_get(c);
  return valid(v) ? v : 0;
}

static int fat_flush(void) {
  if (!F.fat_changed) return 0;
  int err = 0;
  for (uint32_t s = 0; s < F.fatsz / F.bps; s++) {
    if (!F.fat_dirty[s]) continue;
    for (uint32_t k = 0; k < F.nfats; k++) {
      int r = dev_write(F.fat_off + (uint64_t)k * F.fatsz + (uint64_t)s * F.bps, F.fat + (size_t)s * F.bps, F.bps);
      if (r) err = r;
    }
    F.fat_dirty[s] = 0;
  }
  F.fat_changed = false;
  return err;
}

static struct {
  uint32_t start, n, c;
} ccache;

static uint32_t alloc_cluster(uint32_t prev) {
  for (uint32_t i = 0; i < F.nclus; i++) {
    uint32_t c = F.next_free + i;
    if (c >= F.nclus + 2) c -= F.nclus;
    if (fat_get(c) != 0) continue;
    fat_set(c, eoc());
    if (prev) fat_set(prev, c);
    if (F.free_count) F.free_count--;
    F.next_free = c + 1 >= F.nclus + 2 ? 2 : c + 1;
    return c;
  }
  return 0;
}

static void free_chain(uint32_t c) {
  for (uint32_t steps = 0; valid(c) && steps < F.nclus; steps++) {
    uint32_t v = fat_get(c);
    if (v == 0) break; /* already free: chain was corrupt */
    fat_set(c, 0);
    F.free_count++;
    c = valid(v) ? v : 0;
  }
  ccache.start = 0;
}

/* Cluster number `n` (0-based) of the chain at `start`, or 0. */
static uint32_t nth_cluster(uint32_t start, uint32_t n) {
  uint32_t i = 0, c = start;
  if (ccache.start == start && ccache.n <= n && valid(ccache.c)) i = ccache.n, c = ccache.c;
  for (; i < n && c; i++) c = next(c);
  if (c) ccache.start = start, ccache.n = n, ccache.c = c;
  return c;
}

static uint32_t chain_len(uint32_t start) {
  uint32_t n = 0;
  for (uint32_t c = start; valid(c) && n < F.nclus; c = next(c)) n++;
  return n;
}

static uint64_t clus_off(uint32_t c) { return F.data_off + (uint64_t)(c - 2) * F.cs; }

/* ---------------- time ---------------- */

static int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

static int64_t fat_to_unix(uint16_t date, uint16_t tm) {
  if (!date) return 0;
  unsigned m = (date >> 5) & 15, d = date & 31;
  if (m < 1 || m > 12 || d < 1) return 0;
  return days_from_civil(1980 + (date >> 9), m, d) * 86400 + (tm >> 11) * 3600 + ((tm >> 5) & 63) * 60 + (tm & 31) * 2;
}

static void unix_to_fat(int64_t t, uint16_t *date, uint16_t *tm) {
  if (t < 315532800) t = 315532800; /* 1980-01-01 */
  int64_t days = t / 86400, secs = t % 86400;
  int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = (int)(yoe + era * 400);
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  unsigned d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
  if (y > 2107) y = 2107;
  *date = (uint16_t)((y - 1980) << 9 | m << 5 | d);
  if (tm) *tm = (uint16_t)((secs / 3600) << 11 | ((secs / 60) % 60) << 5 | (secs % 60) / 2);
}

static void touch(struct de *e, bool modify) {
  uint16_t d, t;
  unix_to_fat(time(NULL), &d, &t);
  e->adate = d;
  if (modify) {
    e->mdate = d, e->mtime = t;
    e->attr |= A_ARCH;
  }
}

/* ---------------- directories ---------------- */

struct dir {
  uint32_t clus; /* first cluster; 0 = FAT12/16 fixed root region */
  struct de *e;
  uint32_t n;
  uint32_t *cl; /* clusters of the chain */
  uint32_t ncl;
};

static uint32_t de_start(const struct de *e) { return (F.type == 32 ? (uint32_t)e->clus_hi << 16 : 0) | e->clus_lo; }

static void de_set_start(struct de *e, uint32_t c) {
  e->clus_lo = c & 0xFFFF;
  e->clus_hi = F.type == 32 ? c >> 16 : 0;
}

static bool is_dot(const struct de *e) {
  return e->name[0] == '.' && (!memcmp(e->name, ".          ", 11) || !memcmp(e->name, "..         ", 11));
}

static bool live(const struct de *e) {
  return e->name[0] != 0 && e->name[0] != 0xE5 && e->attr != A_LFN && !(e->attr & A_VOLUME) && !is_dot(e);
}

static uint32_t root_clus(void) { return F.type == 32 ? F.root_clus : 0; }

static void dir_free(struct dir *d) {
  free(d->e);
  free(d->cl);
  memset(d, 0, sizeof(*d));
}

static int dir_load(uint32_t clus, struct dir *d) {
  memset(d, 0, sizeof(*d));
  d->clus = clus;
  if (clus == 0) {
    if (F.type == 32) return -ESTALE;
    d->n = F.root_ents;
    d->e = malloc((size_t)d->n * 32 + 32);
    if (!d->e) return -ENOMEM;
    int r = dev_read(F.root_off, d->e, (size_t)d->n * 32);
    if (r) dir_free(d);
    return r;
  }
  if (!valid(clus)) return -ESTALE;
  uint32_t per = F.cs / 32, max = (MAX_DIR_ENTRIES + per - 1) / per;
  d->cl = malloc(max * sizeof(uint32_t));
  if (!d->cl) return -ENOMEM;
  for (uint32_t c = clus; c && d->ncl < max; c = next(c)) d->cl[d->ncl++] = c;
  d->n = d->ncl * per;
  d->e = malloc((size_t)d->n * 32 + 32);
  if (!d->e) {
    dir_free(d);
    return -ENOMEM;
  }
  for (uint32_t i = 0; i < d->ncl; i++) {
    int r = dev_read(clus_off(d->cl[i]), (uint8_t *)d->e + (size_t)i * F.cs, F.cs);
    if (r) {
      dir_free(d);
      return r;
    }
  }
  return 0;
}

static uint64_t dir_ent_off(const struct dir *d, uint32_t i) {
  if (d->clus == 0) return F.root_off + (uint64_t)i * 32;
  uint32_t per = F.cs / 32;
  return clus_off(d->cl[i / per]) + (uint64_t)(i % per) * 32;
}

/* Write entries [a, b) back to disk. */
static int dir_write(const struct dir *d, uint32_t a, uint32_t b) {
  uint32_t per = d->clus == 0 ? d->n : F.cs / 32;
  while (a < b) {
    uint32_t end = (a / per + 1) * per;
    if (end > b) end = b;
    int r = dev_write(dir_ent_off(d, a), &d->e[a], (size_t)(end - a) * 32);
    if (r) return r;
    a = end;
  }
  return 0;
}

static int zero_cluster(uint32_t c) { return dev_write(clus_off(c), F.zero, F.cs); }

static int dir_extend(struct dir *d) {
  uint32_t per = F.cs / 32;
  if (d->clus == 0 || d->n + per > MAX_DIR_ENTRIES) return -ENOSPC;
  uint32_t c = alloc_cluster(d->cl[d->ncl - 1]);
  if (!c) return -ENOSPC;
  int r = zero_cluster(c);
  if (r) return r;
  struct de *ne = realloc(d->e, (size_t)(d->n + per) * 32 + 32);
  if (!ne) return -ENOMEM;
  d->e = ne;
  memset(&d->e[d->n], 0, (size_t)per * 32);
  d->cl[d->ncl++] = c;
  d->n += per;
  return 0;
}

/* Find `count` consecutive free slots, growing the directory if needed. */
static int dir_alloc(struct dir *d, uint32_t count, uint32_t *first) {
  for (;;) {
    uint32_t run = 0;
    for (uint32_t i = 0; i < d->n; i++) {
      uint8_t c = d->e[i].name[0];
      if (c == 0) { /* end marker: everything after is free */
        if (d->n - i >= count) {
          *first = i;
          return 0;
        }
        break;
      }
      if (c == 0xE5) {
        if (++run == count) {
          *first = i + 1 - count;
          return 0;
        }
      } else {
        run = 0;
      }
    }
    int r = dir_extend(d);
    if (r) return r;
  }
}

/* ---------------- names ---------------- */

struct ent {
  uint32_t first, idx; /* first slot (LFN or short) and short entry */
  char name[1024];
};

static size_t put_utf8(char *o, uint32_t cp) {
  if (cp < 0x80) return o[0] = cp, 1;
  if (cp < 0x800) return o[0] = 0xC0 | cp >> 6, o[1] = 0x80 | (cp & 63), 2;
  if (cp < 0x10000) return o[0] = 0xE0 | cp >> 12, o[1] = 0x80 | ((cp >> 6) & 63), o[2] = 0x80 | (cp & 63), 3;
  o[0] = 0xF0 | cp >> 18, o[1] = 0x80 | ((cp >> 12) & 63), o[2] = 0x80 | ((cp >> 6) & 63), o[3] = 0x80 | (cp & 63);
  return 4;
}

static void utf16_to_utf8(const uint16_t *u, size_t n, char *out, size_t cap) {
  size_t o = 0;
  for (size_t i = 0; i < n && o + 5 < cap; i++) {
    uint32_t cp = u[i];
    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000)
      cp = 0x10000 + ((cp - 0xD800) << 10) + (u[++i] - 0xDC00);
    else if (cp >= 0xD800 && cp < 0xE000)
      cp = '?';
    o += put_utf8(out + o, cp);
  }
  out[o] = 0;
}

/* UTF-8 -> UTF-16; returns length or -EINVAL. */
static int utf8_to_utf16(const char *s, uint16_t *u, size_t cap) {
  size_t n = 0;
  const uint8_t *p = (const uint8_t *)s;
  while (*p) {
    uint32_t cp;
    int len;
    if (*p < 0x80)
      cp = *p, len = 1;
    else if ((*p & 0xE0) == 0xC0)
      cp = *p & 0x1F, len = 2;
    else if ((*p & 0xF0) == 0xE0)
      cp = *p & 0x0F, len = 3;
    else if ((*p & 0xF8) == 0xF0)
      cp = *p & 0x07, len = 4;
    else
      return -EINVAL;
    for (int i = 1; i < len; i++) {
      if ((p[i] & 0xC0) != 0x80) return -EINVAL;
      cp = cp << 6 | (p[i] & 0x3F);
    }
    p += len;
    if (cp >= 0x10000) {
      if (n + 2 > cap) return -ENAMETOOLONG;
      cp -= 0x10000;
      u[n++] = 0xD800 + (cp >> 10);
      u[n++] = 0xDC00 + (cp & 0x3FF);
    } else {
      if (n + 1 > cap) return -ENAMETOOLONG;
      u[n++] = (uint16_t)cp;
    }
  }
  return (int)n;
}

static void short_display(const struct de *e, char *out) {
  size_t o = 0;
  int end = 8;
  while (end > 0 && e->name[end - 1] == ' ') end--;
  for (int i = 0; i < end; i++) {
    uint8_t c = e->name[i];
    if (i == 0 && c == 0x05) c = 0xE5;
    if ((e->ntres & 0x08) && c >= 'A' && c <= 'Z') c += 32;
    if (c >= 0x80) c = '_';
    out[o++] = c;
  }
  int xe = 11;
  while (xe > 8 && e->name[xe - 1] == ' ') xe--;
  if (xe > 8) {
    out[o++] = '.';
    for (int i = 8; i < xe; i++) {
      uint8_t c = e->name[i];
      if ((e->ntres & 0x10) && c >= 'A' && c <= 'Z') c += 32;
      if (c >= 0x80) c = '_';
      out[o++] = c;
    }
  }
  out[o] = 0;
}

static uint8_t lfn_sum(const uint8_t *n) {
  uint8_t s = 0;
  for (int i = 0; i < 11; i++) s = (uint8_t)(((s & 1) << 7) + (s >> 1) + n[i]);
  return s;
}

/* Next live entry at or after *pos; assembles long names. */
static bool dir_next(const struct dir *d, uint32_t *pos, struct ent *out) {
  uint16_t lfn[20 * 13 + 1];
  int lfn_n = 0, expect = 0;
  uint8_t sum = 0;
  uint32_t lfn_first = 0;
  for (uint32_t i = *pos; i < d->n; i++) {
    const struct de *e = &d->e[i];
    const uint8_t *raw = (const uint8_t *)e;
    if (e->name[0] == 0) break;
    if (e->name[0] == 0xE5) {
      expect = 0;
      continue;
    }
    if (e->attr == A_LFN) {
      uint8_t ord = raw[0];
      if (ord & 0x40) {
        int cnt = ord & 0x1F;
        if (cnt < 1 || cnt > 20) {
          expect = 0;
          continue;
        }
        lfn_n = cnt * 13;
        expect = cnt;
        sum = raw[13];
        lfn_first = i;
        for (int k = 0; k <= lfn_n; k++) lfn[k] = 0;
      } else if (!expect || (ord & 0x1F) != expect || raw[13] != sum) {
        expect = 0;
        continue;
      }
      int seq = (ord & 0x1F) - 1;
      for (int k = 0; k < 13; k++) lfn[seq * 13 + k] = le16(raw + lfn_off[k]);
      expect = (ord & 0x1F) - 1;
      if (expect == 0) expect = -1; /* complete; short entry must follow */
      continue;
    }
    if (!live(e)) {
      expect = 0;
      continue;
    }
    out->idx = i;
    if (expect == -1 && lfn_sum(e->name) == sum) {
      int len = 0;
      while (len < lfn_n && lfn[len] != 0 && lfn[len] != 0xFFFF) len++;
      utf16_to_utf8(lfn, len, out->name, sizeof(out->name));
      out->first = lfn_first;
    } else {
      short_display(e, out->name);
      out->first = i;
    }
    *pos = i + 1;
    return true;
  }
  *pos = d->n;
  return false;
}

static bool name_eq(const char *a, const char *b) {
  for (;; a++, b++) {
    unsigned char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return false;
    if (!x) return true;
  }
}

static bool dir_find(const struct dir *d, const char *name, struct ent *out) {
  uint32_t pos = 0;
  while (dir_next(d, &pos, out)) {
    if (name_eq(out->name, name)) return true;
    char s[16];
    short_display(&d->e[out->idx], s);
    if (name_eq(s, name)) return true;
  }
  return false;
}

static bool short_char_ok(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80 ||
         strchr("$%'-_@~`!(){}^#&", c);
}

static int check_name(const char *name) {
  size_t len = strlen(name);
  if (!len || !strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
  if (len > 255) return -ENAMETOOLONG;
  for (const unsigned char *p = (const unsigned char *)name; *p; p++)
    if (*p < 0x20 || strchr("\\/:*?\"<>|", *p)) return -EINVAL;
  return 0;
}

/* Is `name` a plain 8.3 name? Fills the short name and case flags. */
static bool fits_83(const char *name, uint8_t sn[11], uint8_t *ntres) {
  const char *dot = strrchr(name, '.');
  size_t blen = dot ? (size_t)(dot - name) : strlen(name), elen = dot ? strlen(dot + 1) : 0;
  if (blen < 1 || blen > 8 || elen > 3 || (dot && elen == 0)) return false;
  int bl = 0, bu = 0, el = 0, eu = 0;
  memset(sn, ' ', 11);
  for (size_t i = 0; i < blen; i++) {
    unsigned char c = name[i];
    if (c >= 0x80 || !short_char_ok(c)) return false;
    if (c >= 'a' && c <= 'z')
      bl = 1, c -= 32;
    else if (c >= 'A' && c <= 'Z')
      bu = 1;
    sn[i] = c;
  }
  for (size_t i = 0; i < elen; i++) {
    unsigned char c = dot[1 + i];
    if (c >= 0x80 || !short_char_ok(c)) return false;
    if (c >= 'a' && c <= 'z')
      el = 1, c -= 32;
    else if (c >= 'A' && c <= 'Z')
      eu = 1;
    sn[8 + i] = c;
  }
  if ((bl && bu) || (el && eu)) return false;
  if (sn[0] == 0xE5) sn[0] = 0x05;
  *ntres = (bl ? 0x08 : 0) | (el ? 0x10 : 0);
  return true;
}

static bool short_exists(const struct dir *d, const uint8_t sn[11]) {
  for (uint32_t i = 0; i < d->n; i++) {
    if (d->e[i].name[0] == 0) break;
    if (d->e[i].name[0] != 0xE5 && d->e[i].attr != A_LFN && !memcmp(d->e[i].name, sn, 11)) return true;
  }
  return false;
}

static int gen_short(const struct dir *d, const char *name, uint8_t sn[11]) {
  char base[9] = {0}, ext[4] = {0};
  size_t bn = 0, en = 0;
  while (*name == '.') name++;
  const char *dot = strrchr(name, '.');
  for (const unsigned char *p = (const unsigned char *)name; *p && (!dot || p < (const unsigned char *)dot); p++) {
    if (*p == ' ' || *p == '.') continue;
    if (*p >= 0x80) {
      while ((p[1] & 0xC0) == 0x80) p++;
    }
    if (bn < 6) base[bn++] = *p >= 0x80 || !short_char_ok(*p) ? '_' : (*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
  }
  if (dot)
    for (const unsigned char *p = (const unsigned char *)dot + 1; *p && en < 3; p++) {
      if (*p == ' ') continue;
      if (*p >= 0x80) {
        while ((p[1] & 0xC0) == 0x80) p++;
      }
      ext[en++] = *p >= 0x80 || !short_char_ok(*p) ? '_' : (*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    }
  if (!bn) base[bn++] = '_';
  for (unsigned n = 1; n < 1000000; n++) {
    char tail[8];
    int tl = snprintf(tail, sizeof(tail), "~%u", n);
    size_t keep = bn;
    if (keep + tl > 8) keep = 8 - tl;
    memset(sn, ' ', 11);
    memcpy(sn, base, keep);
    memcpy(sn + keep, tail, tl);
    memcpy(sn + 8, ext, en);
    if (!short_exists(d, sn)) return 0;
  }
  return -EEXIST;
}

/* Build the entries for `name` (long-name slots + short entry from `tmpl`). */
static int build_entries(const struct dir *d, const char *name, const struct de *tmpl, struct de *out, int *count) {
  int r = check_name(name);
  if (r) return r;
  struct de s = *tmpl;
  uint8_t ntres = 0;
  s.ntres &= ~0x18;
  if (fits_83(name, s.name, &ntres)) {
    s.ntres |= ntres;
    out[0] = s;
    *count = 1;
    return 0;
  }
  uint16_t u[256];
  int len = utf8_to_utf16(name, u, 255);
  if (len < 0) return len;
  r = gen_short(d, name, s.name);
  if (r) return r;
  int nl = (len + 12) / 13;
  uint8_t sum = lfn_sum(s.name);
  for (int k = 0; k < nl; k++) {
    int seq = nl - k; /* on disk the last part comes first */
    uint8_t *raw = (uint8_t *)&out[k];
    memset(raw, 0, 32);
    raw[0] = (uint8_t)(seq | (k == 0 ? 0x40 : 0));
    raw[11] = A_LFN;
    raw[13] = sum;
    for (int j = 0; j < 13; j++) {
      int ci = (seq - 1) * 13 + j;
      uint16_t ch = ci < len ? u[ci] : ci == len ? 0 : 0xFFFF;
      raw[lfn_off[j]] = ch & 0xFF;
      raw[lfn_off[j] + 1] = ch >> 8;
    }
  }
  out[nl] = s;
  *count = nl + 1;
  return 0;
}

static int dir_add(struct dir *d, const char *name, const struct de *tmpl, uint32_t *idx) {
  struct de ents[21];
  int count, r = build_entries(d, name, tmpl, ents, &count);
  if (r) return r;
  uint32_t first;
  if ((r = dir_alloc(d, count, &first))) return r;
  memcpy(&d->e[first], ents, (size_t)count * 32);
  *idx = first + count - 1;
  return dir_write(d, first, first + count);
}

static int dir_remove(struct dir *d, const struct ent *e) {
  for (uint32_t i = e->first; i <= e->idx; i++) d->e[i].name[0] = 0xE5;
  return dir_write(d, e->first, e->idx + 1);
}

static bool dir_empty(const struct dir *d) {
  uint32_t pos = 0;
  struct ent e;
  return !dir_next(d, &pos, &e);
}

/* ---------------- inode table ---------------- */

#define NT_BUCKETS 4096
#define NT_MAX 65536
struct nent {
  uint64_t ino;
  uint32_t dclus, idx;
  struct nent *next;
};
static struct nent *nt[NT_BUCKETS];
static size_t nt_count;

static struct nent *nt_find(uint64_t ino) {
  for (struct nent *e = nt[ino % NT_BUCKETS]; e; e = e->next)
    if (e->ino == ino) return e;
  return NULL;
}

static void nt_clear(void) {
  for (int b = 0; b < NT_BUCKETS; b++)
    while (nt[b]) {
      struct nent *e = nt[b];
      nt[b] = e->next;
      free(e);
    }
  nt_count = 0;
}

static void nt_set(uint64_t ino, uint32_t dclus, uint32_t idx) {
  struct nent *e = nt_find(ino);
  if (!e) {
    if (nt_count >= NT_MAX) nt_clear(); /* numbers are recoverable by scanning */
    e = malloc(sizeof(*e));
    if (!e) return;
    e->ino = ino;
    e->next = nt[ino % NT_BUCKETS];
    nt[ino % NT_BUCKETS] = e;
    nt_count++;
  }
  e->dclus = dclus;
  e->idx = idx;
}

/* Repoint (or with nd == ~0 drop) every table entry naming a location. */
static void nt_relocate(uint32_t od, uint32_t oi, uint32_t nd, uint32_t ni) {
  for (int b = 0; b < NT_BUCKETS; b++)
    for (struct nent **pp = &nt[b]; *pp;) {
      struct nent *e = *pp;
      if (e->dclus == od && e->idx == oi) {
        if (nd == ~0u) {
          *pp = e->next;
          free(e);
          nt_count--;
          continue;
        }
        e->dclus = nd;
        e->idx = ni;
      }
      pp = &e->next;
    }
}

static uint64_t ino_of(uint32_t dclus, uint32_t idx, const struct de *e) {
  uint32_t s = de_start(e);
  return valid(s) ? s : POS_FLAG | (uint64_t)dclus << 16 | idx;
}

/* Find the entry that owns cluster `c` by walking the tree. */
static int scan_for(uint32_t c, uint32_t *dclus, uint32_t *idx) {
  uint32_t cap = 1024, qh = 0, qt = 0;
  uint32_t *q = malloc(cap * sizeof(uint32_t));
  uint8_t *seen = calloc((F.nclus + 2 + 7) / 8, 1);
  int res = -ESTALE;
  if (!q || !seen) {
    free(q);
    free(seen);
    return -ENOMEM;
  }
  q[qt++] = root_clus();
  while (qh < qt && res == -ESTALE) {
    struct dir d;
    uint32_t dc = q[qh++];
    if (dir_load(dc, &d)) continue;
    for (uint32_t i = 0; i < d.n && d.e[i].name[0]; i++) {
      if (!live(&d.e[i])) continue;
      uint32_t s = de_start(&d.e[i]);
      if (s == c) {
        *dclus = dc, *idx = i, res = 0;
        break;
      }
      if ((d.e[i].attr & A_DIR) && valid(s) && !(seen[s / 8] & (1 << (s % 8)))) {
        seen[s / 8] |= 1 << (s % 8);
        if (qt == cap) {
          uint32_t *nq = realloc(q, (cap *= 2) * sizeof(uint32_t));
          if (!nq) break;
          q = nq;
        }
        q[qt++] = s;
      }
    }
    dir_free(&d);
  }
  free(q);
  free(seen);
  return res;
}

struct node {
  uint64_t ino;
  bool root;
  uint32_t dclus, idx;
  struct dir d; /* directory containing the entry */
  struct de *e;
};

static int node_get(uint64_t ino, struct node *n) {
  memset(n, 0, sizeof(*n));
  n->ino = ino;
  if (ino == ROOT_INO) {
    n->root = true;
    return 0;
  }
  struct nent *t = nt_find(ino);
  if (t) {
    n->dclus = t->dclus, n->idx = t->idx;
  } else if (ino & POS_FLAG) {
    n->dclus = (ino >> 16) & 0x0FFFFFFF;
    n->idx = ino & 0xFFFF;
  } else if (ino < (1ULL << 32) && valid((uint32_t)ino)) {
    int r = scan_for((uint32_t)ino, &n->dclus, &n->idx);
    if (r) return r;
    nt_set(ino, n->dclus, n->idx);
  } else {
    return -ESTALE;
  }
  int r = dir_load(n->dclus, &n->d);
  if (r) return r;
  if (n->idx >= n->d.n || !live(&n->d.e[n->idx])) {
    dir_free(&n->d);
    return -ESTALE;
  }
  n->e = &n->d.e[n->idx];
  return 0;
}

static void node_put(struct node *n) { dir_free(&n->d); }

static int node_save(struct node *n) { return dir_write(&n->d, n->idx, n->idx + 1); }

static bool node_isdir(const struct node *n) { return n->root || (n->e->attr & A_DIR); }

static uint32_t node_dirclus(const struct node *n) { return n->root ? root_clus() : de_start(n->e); }

static void fill_attr(uint64_t ino, const struct de *e, struct ufs_attr *a) {
  memset(a, 0, sizeof(*a));
  a->ino = ino;
  a->nlink = 1;
  if (!e) {
    a->mode = S_IFDIR | 0755;
    a->nlink = 2;
    a->size = F.type == 32 ? (uint64_t)chain_len(F.root_clus) * F.cs : F.root_bytes;
  } else if (e->attr & A_DIR) {
    a->mode = S_IFDIR | 0755;
    a->nlink = 2;
    a->size = (uint64_t)chain_len(de_start(e)) * F.cs;
  } else {
    a->mode = S_IFREG | 0755;
    a->size = e->size;
  }
  if (e && (e->attr & A_RO)) a->mode &= ~0222;
  if (F.ro) a->mode &= ~0222;
  a->blocks = (a->size + 511) / 512;
  if (e) {
    a->mtime = fat_to_unix(e->mdate, e->mtime);
    a->atime = e->adate ? fat_to_unix(e->adate, 0) : a->mtime;
    a->ctime = e->cdate ? fat_to_unix(e->cdate, e->ctime) : a->mtime;
  }
}

/* ---------------- file data ---------------- */

/* Make the chain at *start at least `need` clusters long; returns its length. */
static uint32_t chain_grow(uint32_t *start, uint32_t need) {
  uint32_t have = 0, last = 0;
  for (uint32_t c = *start; valid(c) && have < F.nclus; c = next(c)) last = c, have++;
  while (have < need) {
    uint32_t c = alloc_cluster(last);
    if (!c) break;
    if (!last) *start = c;
    last = c;
    have++;
  }
  return have;
}

/* Keep the first `keep` clusters of the chain at *start. */
static void chain_trim(uint32_t *start, uint32_t keep) {
  if (!valid(*start)) {
    *start = 0;
    return;
  }
  if (keep == 0) {
    free_chain(*start);
    *start = 0;
    return;
  }
  uint32_t c = nth_cluster(*start, keep - 1);
  if (!c) return;
  uint32_t rest = next(c);
  fat_set(c, eoc());
  if (rest) free_chain(rest);
  ccache.start = 0;
}

static int write_range(uint32_t start, uint64_t off, const uint8_t *buf, uint64_t len) {
  while (len) {
    uint32_t c = nth_cluster(start, (uint32_t)(off / F.cs));
    if (!c) return -EIO;
    uint32_t co = off % F.cs;
    uint64_t chunk = F.cs - co < len ? F.cs - co : len;
    int r = dev_write(clus_off(c) + co, buf ? buf : F.zero, chunk);
    if (r) return r;
    off += chunk, len -= chunk;
    if (buf) buf += chunk;
  }
  return 0;
}

/* Resize a file to `size` (zero-filling growth). */
static int file_resize(struct node *n, uint64_t size) {
  struct de *e = n->e;
  uint32_t start = de_start(e), old = e->size;
  if (size > MAX_FILE) return -EFBIG;
  uint32_t need = (uint32_t)((size + F.cs - 1) / F.cs);
  int r = 0;
  if (size > old) {
    if (chain_grow(&start, need) < need) {
      chain_trim(&start, (old + F.cs - 1) / F.cs);
      r = -ENOSPC;
    } else {
      r = write_range(start, old, NULL, size - old);
    }
  } else {
    chain_trim(&start, need);
  }
  if (!r) e->size = (uint32_t)size;
  de_set_start(e, start);
  if (valid(start)) nt_set(start, n->dclus, n->idx);
  touch(e, true);
  int w = node_save(n);
  int f = fat_flush();
  return r ? r : w ? w : f;
}

/* ---------------- operations ---------------- */

static int op_root(void *ctx, struct ufs_attr *a) {
  fill_attr(ROOT_INO, NULL, a);
  return 0;
}

static int op_getattr(void *ctx, uint64_t ino, struct ufs_attr *a) {
  struct node n;
  int r = node_get(ino, &n);
  if (r) return r;
  fill_attr(ino, n.e, a);
  node_put(&n);
  return 0;
}

/* Load the directory `ino` refers to. */
static int open_dir(uint64_t ino, struct dir *d) {
  struct node n;
  int r = node_get(ino, &n);
  if (r) return r;
  if (!node_isdir(&n)) {
    node_put(&n);
    return -ENOTDIR;
  }
  uint32_t c = node_dirclus(&n);
  node_put(&n);
  return dir_load(c, d);
}

static int op_lookup(void *ctx, uint64_t dino, const char *name, struct ufs_attr *a) {
  struct dir d;
  int r = open_dir(dino, &d);
  if (r) return r;
  struct ent e;
  if (!dir_find(&d, name, &e)) {
    dir_free(&d);
    return -ENOENT;
  }
  uint64_t ino = ino_of(d.clus, e.idx, &d.e[e.idx]);
  nt_set(ino, d.clus, e.idx);
  fill_attr(ino, &d.e[e.idx], a);
  dir_free(&d);
  return 0;
}

static ssize_t op_read(void *ctx, uint64_t ino, uint64_t off, void *buf, size_t len, struct ufs_attr *a) {
  struct node n;
  int r = node_get(ino, &n);
  if (r) return r;
  if (node_isdir(&n)) {
    node_put(&n);
    return -EISDIR;
  }
  uint32_t size = n.e->size, start = de_start(n.e);
  fill_attr(ino, n.e, a);
  node_put(&n);
  if (off >= size) return 0;
  if (len > size - off) len = size - off;
  size_t done = 0;
  while (done < len) {
    uint32_t c = nth_cluster(start, (uint32_t)((off + done) / F.cs));
    if (!c) break; /* chain shorter than the size: corrupt, return what we have */
    uint32_t co = (off + done) % F.cs;
    size_t chunk = F.cs - co < len - done ? F.cs - co : len - done;
    if ((r = dev_read(clus_off(c) + co, (uint8_t *)buf + done, chunk))) return done ? (ssize_t)done : r;
    done += chunk;
  }
  return (ssize_t)done;
}

static ssize_t op_write(void *ctx, uint64_t ino, uint64_t off, const void *buf, size_t len, struct ufs_attr *a) {
  if (F.ro) return -EROFS;
  struct node n;
  int r = node_get(ino, &n);
  if (r) return r;
  if (node_isdir(&n)) {
    r = -EISDIR;
    goto out;
  }
  if (n.e->attr & A_RO) {
    r = -EACCES;
    goto out;
  }
  if (off >= MAX_FILE) {
    r = -EFBIG;
    goto out;
  }
  if (len > MAX_FILE - off) len = MAX_FILE - off;
  struct de *e = n.e;
  uint32_t start = de_start(e), size = e->size;
  uint64_t end = off + len;
  uint32_t need = (uint32_t)((end + F.cs - 1) / F.cs);
  uint32_t have = chain_grow(&start, need);
  if ((uint64_t)have * F.cs < end) {
    if ((uint64_t)have * F.cs <= off) {
      chain_trim(&start, (size + F.cs - 1) / F.cs);
      de_set_start(e, start);
      fat_flush();
      r = -ENOSPC;
      goto out;
    }
    end = (uint64_t)have * F.cs;
    len = end - off;
  }
  if (off > size) r = write_range(start, size, NULL, off - size);
  if (!r) r = write_range(start, off, buf, len);
  if (!r && end > size) e->size = (uint32_t)end;
  de_set_start(e, start);
  if (valid(start)) nt_set(start, n.dclus, n.idx);
  touch(e, true);
  int w = node_save(&n);
  int f = fat_flush();
  if (!r) r = w ? w : f;
  fill_attr(ino, e, a);
out:
  node_put(&n);
  return r ? r : (ssize_t)len;
}

static int op_readdir(void *ctx, uint64_t ino, uint64_t cookie, ufs_filler fill, void *fctx) {
  struct dir d;
  int r = open_dir(ino, &d);
  if (r) return r;
  if (cookie == 0 && !fill(fctx, ino, 1, DT_DIR_, ".", 1)) goto out;
  if (cookie <= 1) {
    uint64_t parent = ROOT_INO;
    if (ino != ROOT_INO && d.n > 1 && d.e[1].name[0] == '.' && valid(de_start(&d.e[1]))) parent = de_start(&d.e[1]);
    if (!fill(fctx, parent, 2, DT_DIR_, "..", 2)) goto out;
    cookie = 2;
  }
  uint32_t pos = cookie - 2 > d.n ? d.n : (uint32_t)(cookie - 2);
  struct ent e;
  while (dir_next(&d, &pos, &e)) {
    const struct de *de = &d.e[e.idx];
    if (!fill(fctx, ino_of(d.clus, e.idx, de), 2 + (uint64_t)e.idx + 1, (de->attr & A_DIR) ? DT_DIR_ : DT_REG_, e.name,
              strlen(e.name)))
      break;
  }
out:
  dir_free(&d);
  return 0;
}

static void new_entry(struct de *e, uint8_t attr) {
  memset(e, 0, sizeof(*e));
  e->attr = attr;
  uint16_t d, t;
  unix_to_fat(time(NULL), &d, &t);
  e->cdate = e->mdate = e->adate = d;
  e->ctime = e->mtime = t;
}

static int op_create(void *ctx, uint64_t dino, const char *name, uint32_t mode, struct ufs_attr *a) {
  if (F.ro) return -EROFS;
  if ((mode & S_IFMT) && !S_ISREG(mode)) return -EPERM;
  struct dir d;
  int r = open_dir(dino, &d);
  if (r) return r;
  struct ent ex;
  uint32_t idx;
  if (dir_find(&d, name, &ex)) {
    r = -EEXIST;
  } else {
    struct de t;
    new_entry(&t, A_ARCH | ((mode & 0222) ? 0 : A_RO));
    r = dir_add(&d, name, &t, &idx);
    int f = fat_flush();
    if (!r) r = f;
  }
  if (!r) {
    uint64_t ino = ino_of(d.clus, idx, &d.e[idx]);
    nt_set(ino, d.clus, idx);
    fill_attr(ino, &d.e[idx], a);
  }
  dir_free(&d);
  return r;
}

static int op_mkdir(void *ctx, uint64_t dino, const char *name, uint32_t mode, struct ufs_attr *a) {
  if (F.ro) return -EROFS;
  struct dir d;
  int r = open_dir(dino, &d);
  if (r) return r;
  struct ent ex;
  if (dir_find(&d, name, &ex) || (r = check_name(name))) {
    if (!r) r = -EEXIST;
    dir_free(&d);
    return r;
  }
  uint32_t c = alloc_cluster(0);
  if (!c) {
    dir_free(&d);
    return -ENOSPC;
  }
  /* child first (a crash leaves a lost cluster, never a broken directory) */
  struct de *blk = calloc(1, F.cs);
  r = blk ? 0 : -ENOMEM;
  if (!r) {
    new_entry(&blk[0], A_DIR);
    memcpy(blk[0].name, ".          ", 11);
    de_set_start(&blk[0], c);
    blk[1] = blk[0];
    memcpy(blk[1].name, "..         ", 11);
    de_set_start(&blk[1], d.clus == root_clus() ? 0 : d.clus);
    r = dev_write(clus_off(c), blk, F.cs);
    free(blk);
  }
  uint32_t idx = 0;
  if (!r) {
    struct de t;
    new_entry(&t, A_DIR);
    de_set_start(&t, c);
    r = dir_add(&d, name, &t, &idx);
  }
  if (r) {
    fat_set(c, 0);
    F.free_count++;
  }
  int f = fat_flush();
  if (!r) r = f;
  if (!r) {
    nt_set(c, d.clus, idx);
    fill_attr(c, &d.e[idx], a);
  }
  dir_free(&d);
  return r;
}

static int remove_entry(uint64_t dino, const char *name, bool want_dir) {
  if (F.ro) return -EROFS;
  struct dir d;
  int r = open_dir(dino, &d);
  if (r) return r;
  struct ent e;
  if (!dir_find(&d, name, &e)) {
    r = -ENOENT;
    goto out;
  }
  struct de *de = &d.e[e.idx];
  bool isdir = de->attr & A_DIR;
  if (want_dir && !isdir) {
    r = -ENOTDIR;
    goto out;
  }
  if (!want_dir && isdir) {
    r = -EISDIR;
    goto out;
  }
  uint32_t start = de_start(de);
  if (isdir) {
    struct dir c;
    if ((r = dir_load(start, &c))) goto out;
    bool empty = dir_empty(&c);
    dir_free(&c);
    if (!empty) {
      r = -ENOTEMPTY;
      goto out;
    }
  }
  if ((r = dir_remove(&d, &e))) goto out;
  if (valid(start)) free_chain(start);
  nt_relocate(d.clus, e.idx, ~0u, 0);
  r = fat_flush();
out:
  dir_free(&d);
  return r;
}

static int op_unlink(void *ctx, uint64_t dino, const char *name) { return remove_entry(dino, name, false); }
static int op_rmdir(void *ctx, uint64_t dino, const char *name) { return remove_entry(dino, name, true); }

/* Is directory cluster `inner` equal to or below directory cluster `outer`? */
static bool dir_within(uint32_t inner, uint32_t outer) {
  for (uint32_t steps = 0; steps < 256 && inner != root_clus() && inner; steps++) {
    if (inner == outer) return true;
    struct dir d;
    if (dir_load(inner, &d)) return false;
    uint32_t up = d.n > 1 && d.e[1].name[0] == '.' ? de_start(&d.e[1]) : 0;
    dir_free(&d);
    inner = up;
  }
  return false;
}

static int op_rename(void *ctx, uint64_t odino, const char *oname, uint64_t ndino, const char *nname) {
  if (F.ro) return -EROFS;
  struct dir od, ndd, *nd = &ndd;
  int r = open_dir(odino, &od);
  if (r) return r;
  bool same = false;
  if (odino == ndino) {
    same = true;
    nd = &od;
  } else if ((r = open_dir(ndino, &ndd))) {
    dir_free(&od);
    return r;
  } else if (ndd.clus == od.clus) {
    dir_free(&ndd);
    same = true;
    nd = &od;
  }
  struct ent src, dst;
  if (!dir_find(&od, oname, &src)) {
    r = -ENOENT;
    goto out;
  }
  struct de s = od.e[src.idx];
  bool sdir = s.attr & A_DIR;
  uint32_t sstart = de_start(&s);
  if (sdir && !same && dir_within(nd->clus, sstart)) {
    r = -EINVAL;
    goto out;
  }
  if ((r = check_name(nname))) goto out;
  if (dir_find(nd, nname, &dst) && !(same && dst.idx == src.idx)) {
    struct de *t = &nd->e[dst.idx];
    bool tdir = t->attr & A_DIR;
    if (sdir && !tdir) {
      r = -ENOTDIR;
      goto out;
    }
    if (!sdir && tdir) {
      r = -EISDIR;
      goto out;
    }
    uint32_t tstart = de_start(t);
    if (tdir) {
      struct dir c;
      if ((r = dir_load(tstart, &c))) goto out;
      bool empty = dir_empty(&c);
      dir_free(&c);
      if (!empty) {
        r = -ENOTEMPTY;
        goto out;
      }
    }
    if ((r = dir_remove(nd, &dst))) goto out;
    if (valid(tstart)) free_chain(tstart);
    nt_relocate(nd->clus, dst.idx, ~0u, 0);
  }
  uint64_t old_ino = ino_of(od.clus, src.idx, &s);
  uint32_t nidx;
  if ((r = dir_add(nd, nname, &s, &nidx))) goto out;
  /* the source entries are unchanged by dir_add (they are live slots) */
  if ((r = dir_remove(&od, &src))) goto out;
  nt_relocate(od.clus, src.idx, nd->clus, nidx);
  nt_set(old_ino, nd->clus, nidx);
  nt_set(ino_of(nd->clus, nidx, &nd->e[nidx]), nd->clus, nidx);
  if (sdir && !same) {
    struct dir c;
    if (!dir_load(sstart, &c)) {
      if (c.n > 1 && c.e[1].name[0] == '.') {
        de_set_start(&c.e[1], nd->clus == root_clus() ? 0 : nd->clus);
        r = dir_write(&c, 1, 2);
      }
      dir_free(&c);
    }
  }
  int f = fat_flush();
  if (!r) r = f;
out:
  if (!same) dir_free(&ndd);
  dir_free(&od);
  return r;
}

static int op_setattr(void *ctx, uint64_t ino, const struct ufs_req *q, struct ufs_attr *a) {
  struct node n;
  int r = node_get(ino, &n);
  if (r) return r;
  if (n.root) {
    fill_attr(ino, NULL, a);
    return 0;
  }
  if (F.ro) {
    r = -EROFS;
    goto out;
  }
  if (((q->valid & UFS_ATTR_UID) && q->uid != 0) || ((q->valid & UFS_ATTR_GID) && q->gid != 0)) {
    r = -EPERM;
    goto out;
  }
  if (q->valid & UFS_ATTR_SIZE) {
    if (node_isdir(&n)) {
      r = -EISDIR;
      goto out;
    }
    if ((r = file_resize(&n, q->size))) goto out;
  }
  if (q->valid & UFS_ATTR_MODE) {
    if (q->mode & 0222)
      n.e->attr &= ~A_RO;
    else
      n.e->attr |= A_RO;
  }
  if (q->valid & UFS_ATTR_MTIME) unix_to_fat(q->mtime, &n.e->mdate, &n.e->mtime);
  if (q->valid & UFS_ATTR_ATIME) unix_to_fat(q->atime, &n.e->adate, NULL);
  r = node_save(&n);
  fill_attr(ino, n.e, a);
out:
  node_put(&n);
  return r;
}

static int op_statfs(void *ctx, struct ufs_statfs *st) {
  st->blocks = F.nclus;
  st->bfree = F.free_count;
  st->bsize = F.cs;
  st->files = 0;
  st->ffree = 0;
  st->namelen = 255;
  return 0;
}

static void fsinfo_update(void) {
  if (F.type != 32 || !F.fsinfo_sec || F.fsinfo_sec == 0xFFFF || F.ro) return;
  uint8_t s[512];
  uint64_t off = (uint64_t)F.fsinfo_sec * F.bps;
  if (dev_read(off, s, 512) || le32(s) != 0x41615252 || le32(s + 484) != 0x61417272) return;
  uint32_t v[2] = {F.free_count, F.next_free};
  memcpy(s + 488, v, 8);
  dev_write(off, s, 512);
}

static int op_sync(void *ctx) {
  if (F.ro) return 0;
  int r = fat_flush();
  fsinfo_update();
  if (fsync(F.fd) < 0 && !r) r = -EIO;
  return r;
}

/* FAT[1] "clean" bit: cleared while mounted read-write. */
static uint32_t clean_bit(void) { return F.type == 32 ? 0x08000000 : F.type == 16 ? 0x8000 : 0; }

static void set_clean(bool clean) {
  uint32_t bit = clean_bit();
  if (!bit || F.ro) return;
  /* boot-sector state flag, as maintained by Linux and checked by fsck.fat */
  uint8_t flag;
  uint64_t off = F.type == 32 ? 0x41 : 0x25;
  if (!dev_read(off, &flag, 1)) {
    flag = clean ? flag & ~1 : flag | 1;
    dev_write(off, &flag, 1);
  }
  uint32_t v = F.type == 32 ? le32(F.fat + 4) : fat_get(1);
  v = clean ? v | bit : v & ~bit;
  if (F.type == 32) {
    uint8_t *p = F.fat + 4;
    p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24;
    mark_dirty(4, 4);
  } else {
    fat_set(1, v);
  }
}

static void op_shutdown(void *ctx) {
  if (F.ro) return;
  set_clean(true);
  op_sync(ctx);
}

/* ---------------- repair after an unclean shutdown ---------------- */

static uint8_t *used;
static unsigned fix_files, fix_dirs;

static bool test_used(uint32_t c) { return used[c / 8] & (1 << (c % 8)); }
static void set_used(uint32_t c) { used[c / 8] |= 1 << (c % 8); }

static void fsck_dir(uint32_t clus, int depth) {
  if (clus) { /* claim the directory's own chain, cutting loops/cross-links */
    uint32_t prev = 0;
    for (uint32_t c = clus; valid(c); c = next(c)) {
      if (test_used(c)) {
        if (prev) fat_set(prev, eoc());
        fix_dirs++;
        break;
      }
      set_used(c);
      prev = c;
    }
  }
  struct dir d;
  if (dir_load(clus, &d)) return;
  for (uint32_t i = 0; i < d.n && d.e[i].name[0]; i++) {
    struct de *e = &d.e[i];
    if (!live(e)) continue;
    uint32_t start = de_start(e);
    if (e->attr & A_DIR) {
      if (!valid(start) || test_used(start) || depth > 64) {
        e->name[0] = 0xE5; /* unusable directory entry */
        dir_write(&d, i, i + 1);
        fix_dirs++;
        continue;
      }
      fsck_dir(start, depth + 1);
      continue;
    }
    uint32_t need = (uint32_t)(((uint64_t)e->size + F.cs - 1) / F.cs), cnt = 0, prev = 0;
    for (uint32_t c = start; valid(c) && !test_used(c) && cnt < need; c = next(c)) {
      set_used(c);
      prev = c;
      cnt++;
    }
    bool changed = false;
    if (cnt < need) {
      e->size = cnt * F.cs;
      changed = true;
    }
    if (cnt == 0 && start) {
      de_set_start(e, 0);
      changed = true;
    } else if (prev && fat_get(prev) < eoc_min()) {
      fat_set(prev, eoc()); /* the rest of the chain becomes a lost chain */
      changed = true;
    }
    if (changed) {
      dir_write(&d, i, i + 1);
      fix_files++;
    }
  }
  dir_free(&d);
}

static void fsck(void) {
  used = calloc((F.nclus + 2 + 7) / 8, 1);
  if (!used) return;
  fix_files = fix_dirs = 0;
  if (F.type == 32) {
    fsck_dir(F.root_clus, 0);
  } else {
    fsck_dir(0, 0);
  }
  unsigned lost = 0;
  for (uint32_t c = 2; c < F.nclus + 2; c++) {
    uint32_t v = fat_get(c);
    if (v != 0 && v != bad() && !test_used(c)) {
      fat_set(c, 0);
      lost++;
    }
  }
  free(used);
  used = NULL;
  fat_flush();
  ufs_log("fatfsd: %s: was not cleanly unmounted: repaired %u file(s), %u dir(s), freed %u lost cluster(s)", F.dev,
          fix_files, fix_dirs, lost);
}

/* ---------------- mount ---------------- */

static int fat_open(void) {
  uint8_t b[512];
  if (dev_read(0, b, 512) || b[510] != 0x55 || b[511] != 0xAA) return -EINVAL;
  F.bps = le16(b + 11);
  uint32_t spc = b[13], rsvd = le16(b + 14), nfats = b[16], root_ents = le16(b + 17);
  uint32_t tot = le16(b + 19) ? le16(b + 19) : le32(b + 32);
  uint32_t fatsz = le16(b + 22) ? le16(b + 22) : le32(b + 36);
  if ((F.bps != 512 && F.bps != 1024 && F.bps != 2048 && F.bps != 4096) || !spc || (spc & (spc - 1)) || !rsvd ||
      !nfats || nfats > 4 || !fatsz || !tot)
    return -EINVAL;
  F.cs = F.bps * spc;
  if (F.cs > 64 * 1024) return -EINVAL;
  F.nfats = nfats;
  F.root_ents = root_ents;
  uint32_t root_secs = (root_ents * 32 + F.bps - 1) / F.bps;
  uint64_t data_sec = rsvd + (uint64_t)nfats * fatsz + root_secs;
  if (data_sec >= tot) return -EINVAL;
  F.nclus = (uint32_t)((tot - data_sec) / spc);
  F.type = F.nclus < 4085 ? 12 : F.nclus < 65525 ? 16 : 32;
  if (F.type == 32 && root_ents) return -EINVAL;
  if (F.type != 32 && !root_ents) return -EINVAL;
  F.fatsz = fatsz * F.bps;
  if ((uint64_t)(F.nclus + 2) * (F.type == 12 ? 3 : F.type == 16 ? 4 : 8) / 2 > F.fatsz) return -EINVAL;
  F.fat_off = (uint64_t)rsvd * F.bps;
  F.root_off = F.fat_off + (uint64_t)nfats * F.fatsz;
  F.root_bytes = root_secs * F.bps;
  F.data_off = F.root_off + F.root_bytes;
  if (F.type == 32) {
    F.root_clus = le32(b + 44);
    F.fsinfo_sec = le16(b + 48);
    if (!valid(F.root_clus)) return -EINVAL;
  }
  off_t devsize = lseek(F.fd, 0, SEEK_END);
  if (devsize > 0 && (uint64_t)devsize < (uint64_t)tot * F.bps) {
    ufs_log("fatfsd: %s: filesystem larger than device", F.dev);
    return -EINVAL;
  }
  F.fat = malloc(F.fatsz + 4);
  F.fat_dirty = calloc(F.fatsz / F.bps + 1, 1);
  F.zero = calloc(1, F.cs);
  if (!F.fat || !F.fat_dirty || !F.zero) return -ENOMEM;
  if (dev_read(F.fat_off, F.fat, F.fatsz)) return -EIO;
  F.free_count = 0;
  for (uint32_t c = 2; c < F.nclus + 2; c++)
    if (fat_get(c) == 0) F.free_count++;
  F.next_free = 2;
  if (F.type == 32 && F.fsinfo_sec && F.fsinfo_sec != 0xFFFF) {
    uint8_t s[512];
    if (!dev_read((uint64_t)F.fsinfo_sec * F.bps, s, 512) && le32(s) == 0x41615252 && valid(le32(s + 492)))
      F.next_free = le32(s + 492);
  }
  return 0;
}

static const struct ufs_ops ops = {
    .root = op_root,
    .lookup = op_lookup,
    .getattr = op_getattr,
    .read = op_read,
    .write = op_write,
    .readdir = op_readdir,
    .create = op_create,
    .mkdir = op_mkdir,
    .unlink = op_unlink,
    .rmdir = op_rmdir,
    .rename = op_rename,
    .setattr = op_setattr,
    .statfs = op_statfs,
    .sync = op_sync,
    .shutdown = op_shutdown,
};

int main(int argc, char **argv) {
  int opt;
  while ((opt = getopt(argc, argv, "r")) != -1) {
    if (opt == 'r')
      F.ro = true;
    else
      goto usage;
  }
  if (argc - optind != 2) {
  usage:
    fprintf(stderr, "usage: fatfsd [-r] <device> <mountpoint>\n");
    return 2;
  }
  F.dev = argv[optind];
  const char *mnt = argv[optind + 1];
  F.fd = open(F.dev, (F.ro ? O_RDONLY : O_RDWR) | O_CLOEXEC);
  if (F.fd < 0 && !F.ro && (errno == EROFS || errno == EACCES)) {
    F.ro = true;
    F.fd = open(F.dev, O_RDONLY | O_CLOEXEC);
  }
  if (F.fd < 0) {
    ufs_log("fatfsd: %s: %s", F.dev, strerror(errno));
    return UFS_EXIT_NOT_APPLICABLE;
  }
  int r = fat_open();
  if (r) {
    ufs_log("fatfsd: %s: no FAT filesystem (%s)", F.dev, strerror(-r));
    return UFS_EXIT_NOT_APPLICABLE;
  }
  if (!F.ro) {
    uint32_t bit = clean_bit();
    uint32_t v = F.type == 32 ? le32(F.fat + 4) : fat_get(1);
    if (bit && !(v & bit)) fsck();
    set_clean(false);
    fat_flush();
    fsync(F.fd);
  }
  ufs_log("fatfsd: %s: FAT%d, %u clusters of %u bytes, %u free", F.dev, F.type, F.nclus, F.cs, F.free_count);
  return ufs_serve(F.dev, mnt, &ops, NULL, F.ro);
}
