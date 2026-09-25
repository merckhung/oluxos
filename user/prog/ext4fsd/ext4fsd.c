/*
 * ext4fsd - read-only ext2/ext3/ext4 filesystem server.
 *
 *   ext4fsd <device> <mountpoint>
 *
 * Supports extents and legacy block maps, 32/64-bit group descriptors,
 * flex_bg, hashed directories (read linearly), fast and slow symlinks and
 * device nodes. Everything read from disk is bounds-checked: a corrupt or
 * hostile image yields EIO, never a crash. Journals are not replayed; a
 * volume that needs recovery is served as-is with a warning.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ufs_server.h>
#include <unistd.h>

#define EXT4_MAGIC 0xEF53
#define INCOMPAT_FILETYPE 0x2
#define INCOMPAT_RECOVER 0x4
#define INCOMPAT_JOURNAL_DEV 0x8
#define INCOMPAT_META_BG 0x10
#define INCOMPAT_EXTENTS 0x40
#define INCOMPAT_64BIT 0x80
#define INCOMPAT_MMP 0x100
#define INCOMPAT_FLEX_BG 0x200
#define INCOMPAT_CSUM_SEED 0x2000
#define INCOMPAT_LARGEDIR 0x4000
#define INCOMPAT_INLINE_DATA 0x8000
#define INCOMPAT_ENCRYPT 0x10000
#define INCOMPAT_SUPPORTED                                                                                      \
  (INCOMPAT_FILETYPE | INCOMPAT_RECOVER | INCOMPAT_META_BG | INCOMPAT_EXTENTS | INCOMPAT_64BIT | INCOMPAT_MMP | \
   INCOMPAT_FLEX_BG | INCOMPAT_CSUM_SEED | INCOMPAT_LARGEDIR | INCOMPAT_INLINE_DATA | INCOMPAT_ENCRYPT)

#define FL_ENCRYPT 0x800
#define FL_EXTENTS 0x80000
#define FL_INLINE 0x10000000

#define ROOT_INO 2

static struct {
  int fd;
  const char *dev;
  uint32_t bs, inodes_count, inodes_per_group, blocks_per_group, inode_size, desc_size, ngroups, first_data;
  uint64_t blocks_count, free_blocks;
  uint32_t free_inodes, incompat;
  uint8_t *gdt; /* all group descriptors */
} E;

struct inode {
  uint32_t ino;
  uint16_t mode, links;
  uint32_t uid, gid, flags;
  uint64_t size, blocks;
  int64_t atime, mtime, ctime;
  uint8_t iblock[60];
};

static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int dev_read(uint64_t off, void *buf, size_t len) {
  ssize_t n = pread(E.fd, buf, len, (off_t)off);
  return n == (ssize_t)len ? 0 : -EIO;
}

static int read_block(uint64_t blk, void *buf) {
  if (blk >= E.blocks_count) return -EIO;
  return dev_read(blk * E.bs, buf, E.bs);
}

static int read_inode(uint32_t ino, struct inode *in) {
  if (ino < 1 || ino > E.inodes_count) return -ESTALE;
  uint32_t g = (ino - 1) / E.inodes_per_group, idx = (ino - 1) % E.inodes_per_group;
  if (g >= E.ngroups) return -EIO;
  const uint8_t *gd = E.gdt + (size_t)g * E.desc_size;
  uint64_t table = le32(gd + 8) | (E.desc_size >= 64 ? (uint64_t)le32(gd + 0x28) << 32 : 0);
  uint64_t off = table * E.bs + (uint64_t)idx * E.inode_size;
  if (table == 0 || table >= E.blocks_count) return -EIO;
  uint8_t raw[256];
  size_t n = E.inode_size < sizeof(raw) ? E.inode_size : sizeof(raw);
  int r = dev_read(off, raw, n);
  if (r) return r;
  memset(in, 0, sizeof(*in));
  in->ino = ino;
  in->mode = le16(raw);
  in->uid = le16(raw + 2) | (uint32_t)le16(raw + 120) << 16;
  in->gid = le16(raw + 24) | (uint32_t)le16(raw + 122) << 16;
  in->size = le32(raw + 4) | (uint64_t)le32(raw + 108) << 32;
  in->atime = (int32_t)le32(raw + 8);
  in->ctime = (int32_t)le32(raw + 12);
  in->mtime = (int32_t)le32(raw + 16);
  in->links = le16(raw + 26);
  in->blocks = le32(raw + 28) | (uint64_t)le16(raw + 116) << 32;
  in->flags = le32(raw + 32);
  memcpy(in->iblock, raw + 40, 60);
  if (in->mode == 0 || (in->links == 0 && !S_ISDIR(in->mode))) return -ESTALE; /* free inode */
  return 0;
}

/* ---------------- block mapping ---------------- */

/* Map a logical block; *phys = 0 for a hole or an unwritten extent. */
static int map_extent(const uint8_t *node, size_t node_len, uint32_t lblk, uint64_t *phys, int depth_left) {
  if (node_len < 12 || le16(node) != 0xF30A) return -EIO;
  uint16_t entries = le16(node + 2), depth = le16(node + 6);
  if (12 + (size_t)entries * 12 > node_len || depth_left < 0) return -EIO;
  if (depth == 0) {
    for (uint16_t i = 0; i < entries; i++) {
      const uint8_t *x = node + 12 + i * 12;
      uint32_t first = le32(x);
      uint16_t len = le16(x + 4);
      bool unwritten = len > 32768;
      if (unwritten) len -= 32768;
      if (lblk >= first && lblk - first < len) {
        if (unwritten) {
          *phys = 0;
          return 0;
        }
        uint64_t start = (uint64_t)le16(x + 6) << 32 | le32(x + 8);
        *phys = start + (lblk - first);
        return *phys < E.blocks_count ? 0 : -EIO;
      }
    }
    *phys = 0;
    return 0;
  }
  /* index node: last entry whose first block <= lblk */
  int pick = -1;
  for (uint16_t i = 0; i < entries; i++)
    if (le32(node + 12 + i * 12) <= lblk) pick = i;
  if (pick < 0) {
    *phys = 0;
    return 0;
  }
  const uint8_t *x = node + 12 + pick * 12;
  uint64_t leaf = le32(x + 4) | (uint64_t)le16(x + 8) << 32;
  uint8_t *buf = malloc(E.bs);
  if (!buf) return -ENOMEM;
  int r = read_block(leaf, buf);
  if (!r) r = map_extent(buf, E.bs, lblk, phys, depth_left - 1);
  free(buf);
  return r;
}

static int map_indirect(uint64_t blk, int level, uint32_t idx, uint64_t *phys) {
  if (!blk) {
    *phys = 0;
    return 0;
  }
  uint32_t per = E.bs / 4, span = 1;
  for (int i = 1; i < level; i++) span *= per;
  uint8_t *buf = malloc(E.bs);
  if (!buf) return -ENOMEM;
  int r = read_block(blk, buf);
  if (!r) {
    uint32_t next = le32(buf + (idx / span) * 4);
    r = level == 1 ? (*phys = next, next < E.blocks_count ? 0 : -EIO) : map_indirect(next, level - 1, idx % span, phys);
  }
  free(buf);
  return r;
}

static int bmap(const struct inode *in, uint64_t lblk, uint64_t *phys) {
  if (lblk > 0xFFFFFFFFULL) return -EFBIG;
  if (in->flags & FL_EXTENTS) return map_extent(in->iblock, 60, (uint32_t)lblk, phys, 5);
  uint32_t per = E.bs / 4, l = (uint32_t)lblk;
  if (l < 12) {
    *phys = le32(in->iblock + l * 4);
    return *phys < E.blocks_count ? 0 : -EIO;
  }
  l -= 12;
  if (l < per) return map_indirect(le32(in->iblock + 48), 1, l, phys);
  l -= per;
  if ((uint64_t)l < (uint64_t)per * per) return map_indirect(le32(in->iblock + 52), 2, l, phys);
  l -= per * per;
  return map_indirect(le32(in->iblock + 56), 3, l, phys);
}

static ssize_t read_data(const struct inode *in, uint64_t off, void *buf, size_t len) {
  if (in->flags & (FL_INLINE | FL_ENCRYPT)) return -EOPNOTSUPP;
  if (off >= in->size) return 0;
  if (len > in->size - off) len = in->size - off;
  size_t done = 0;
  uint8_t *blk = malloc(E.bs);
  if (!blk) return -ENOMEM;
  while (done < len) {
    uint64_t lb = (off + done) / E.bs, phys;
    uint32_t bo = (off + done) % E.bs;
    size_t chunk = E.bs - bo < len - done ? E.bs - bo : len - done;
    int r = bmap(in, lb, &phys);
    if (!r && phys) r = read_block(phys, blk);
    if (r) {
      free(blk);
      return done ? (ssize_t)done : r;
    }
    if (phys)
      memcpy((uint8_t *)buf + done, blk + bo, chunk);
    else
      memset((uint8_t *)buf + done, 0, chunk);
    done += chunk;
  }
  free(blk);
  return (ssize_t)done;
}

/* ---------------- directories ---------------- */

static uint8_t dtype(uint8_t ft) {
  static const uint8_t map[8] = {0, 8, 4, 2, 6, 1, 12, 10}; /* EXT4_FT_* -> DT_* */
  return ft < 8 ? map[ft] : 0;
}

/* Iterate entries from byte offset `pos`; cb returns false to stop. */
typedef bool (*dir_cb)(void *arg, uint32_t ino, uint8_t ft, const char *name, size_t len, uint64_t next);

static int dir_iterate(const struct inode *dir, uint64_t pos, dir_cb cb, void *arg) {
  if (!S_ISDIR(dir->mode)) return -ENOTDIR;
  if (dir->flags & (FL_INLINE | FL_ENCRYPT)) return -EOPNOTSUPP;
  uint8_t *blk = malloc(E.bs);
  if (!blk) return -ENOMEM;
  int r = 0;
  while (pos < dir->size) {
    uint64_t lb = pos / E.bs, phys;
    uint32_t bo = pos % E.bs;
    if ((r = bmap(dir, lb, &phys))) break;
    if (!phys) { /* hole in a directory: skip the block */
      pos = (lb + 1) * E.bs;
      continue;
    }
    if ((r = read_block(phys, blk))) break;
    while (bo + 8 <= E.bs) {
      const uint8_t *d = blk + bo;
      uint32_t ino = le32(d);
      uint16_t rec = le16(d + 4);
      uint8_t nlen = d[6], ft = d[7];
      if (rec < 8 || (rec & 3) || bo + rec > E.bs || (uint32_t)nlen + 8 > rec) {
        r = -EIO; /* corrupt block */
        goto out;
      }
      pos = lb * E.bs + bo + rec;
      if (ino && nlen && ino <= E.inodes_count) {
        char name[256];
        memcpy(name, d + 8, nlen);
        name[nlen] = 0;
        if (!cb(arg, ino, (E.incompat & INCOMPAT_FILETYPE) ? ft : 0, name, nlen, pos)) goto out;
      }
      bo += rec;
    }
    pos = (lb + 1) * E.bs;
  }
out:
  free(blk);
  return r;
}

struct find_arg {
  const char *name;
  uint32_t ino;
};

static bool find_cb(void *arg, uint32_t ino, uint8_t ft, const char *name, size_t len, uint64_t next) {
  struct find_arg *f = arg;
  if (strcmp(f->name, name)) return true;
  f->ino = ino;
  return false;
}

/* ---------------- operations ---------------- */

static void fill_attr(const struct inode *in, struct ufs_attr *a) {
  memset(a, 0, sizeof(*a));
  a->ino = in->ino;
  a->mode = in->mode & ~0222; /* read-only filesystem */
  a->nlink = in->links ? in->links : 1;
  a->uid = in->uid;
  a->gid = in->gid;
  a->size = in->size;
  a->blocks = in->blocks;
  a->atime = in->atime;
  a->mtime = in->mtime;
  a->ctime = in->ctime;
  if (S_ISCHR(in->mode) || S_ISBLK(in->mode)) {
    uint32_t old = le32(in->iblock), nw = le32(in->iblock + 4);
    if (old)
      a->rdev = ((old >> 8) & 0xFF) << 8 | (old & 0xFF);
    else
      a->rdev = ((nw >> 8) & 0xFFF) << 8 | (nw & 0xFF) | ((nw >> 12) & 0xFFF00);
  }
}

static int op_root(void *ctx, struct ufs_attr *a) {
  struct inode in;
  int r = read_inode(ROOT_INO, &in);
  if (!r) fill_attr(&in, a);
  return r;
}

static int op_getattr(void *ctx, uint64_t ino, struct ufs_attr *a) {
  struct inode in;
  int r = ino > 0xFFFFFFFFULL ? -ESTALE : read_inode((uint32_t)ino, &in);
  if (!r) fill_attr(&in, a);
  return r;
}

static int op_lookup(void *ctx, uint64_t dino, const char *name, struct ufs_attr *a) {
  struct inode dir, in;
  int r = dino > 0xFFFFFFFFULL ? -ESTALE : read_inode((uint32_t)dino, &dir);
  if (r) return r;
  struct find_arg f = {name, 0};
  if ((r = dir_iterate(&dir, 0, find_cb, &f))) return r;
  if (!f.ino) return -ENOENT;
  if ((r = read_inode(f.ino, &in))) return r == -ESTALE ? -EIO : r;
  fill_attr(&in, a);
  return 0;
}

static ssize_t op_read(void *ctx, uint64_t ino, uint64_t off, void *buf, size_t len, struct ufs_attr *a) {
  struct inode in;
  int r = ino > 0xFFFFFFFFULL ? -ESTALE : read_inode((uint32_t)ino, &in);
  if (r) return r;
  if (S_ISDIR(in.mode)) return -EISDIR;
  if (!S_ISREG(in.mode)) return -EINVAL;
  fill_attr(&in, a);
  return read_data(&in, off, buf, len);
}

struct rd_arg {
  ufs_filler fill;
  void *fctx;
};

static bool rd_cb(void *arg, uint32_t ino, uint8_t ft, const char *name, size_t len, uint64_t next) {
  struct rd_arg *r = arg;
  return r->fill(r->fctx, ino, next, dtype(ft), name, len);
}

static int op_readdir(void *ctx, uint64_t ino, uint64_t cookie, ufs_filler fill, void *fctx) {
  struct inode dir;
  int r = ino > 0xFFFFFFFFULL ? -ESTALE : read_inode((uint32_t)ino, &dir);
  if (r) return r;
  struct rd_arg a = {fill, fctx};
  return dir_iterate(&dir, cookie, rd_cb, &a);
}

static ssize_t op_readlink(void *ctx, uint64_t ino, char *buf, size_t size) {
  struct inode in;
  int r = ino > 0xFFFFFFFFULL ? -ESTALE : read_inode((uint32_t)ino, &in);
  if (r) return r;
  if (!S_ISLNK(in.mode)) return -EINVAL;
  if (in.size > 4096) return -EIO;
  size_t len = in.size < size ? in.size : size;
  if (!(in.flags & (FL_EXTENTS | FL_INLINE)) && in.size < 60) { /* fast symlink: target in i_block */
    memcpy(buf, in.iblock, len);
    return (ssize_t)len;
  }
  return read_data(&in, 0, buf, len);
}

static int op_statfs(void *ctx, struct ufs_statfs *st) {
  st->blocks = E.blocks_count;
  st->bfree = E.free_blocks;
  st->bsize = E.bs;
  st->files = E.inodes_count;
  st->ffree = E.free_inodes;
  st->namelen = 255;
  return 0;
}

static const struct ufs_ops ops = {
    .root = op_root,
    .lookup = op_lookup,
    .getattr = op_getattr,
    .read = op_read,
    .readdir = op_readdir,
    .readlink = op_readlink,
    .statfs = op_statfs,
};

static int ext4_open(void) {
  uint8_t sb[1024];
  if (dev_read(1024, sb, sizeof(sb)) || le16(sb + 56) != EXT4_MAGIC) return -EINVAL;
  uint32_t log = le32(sb + 24);
  if (log > 6) return -EINVAL;
  E.bs = 1024u << log;
  E.inodes_count = le32(sb);
  E.first_data = le32(sb + 20);
  E.blocks_per_group = le32(sb + 32);
  E.inodes_per_group = le32(sb + 40);
  E.incompat = le32(sb + 96);
  E.inode_size = le32(sb + 76) >= 1 ? le16(sb + 88) : 128;
  bool is64 = E.incompat & INCOMPAT_64BIT;
  E.blocks_count = le32(sb + 4) | (is64 ? (uint64_t)le32(sb + 0x150) << 32 : 0);
  E.free_blocks = le32(sb + 12) | (is64 ? (uint64_t)le32(sb + 0x158) << 32 : 0);
  E.free_inodes = le32(sb + 16);
  E.desc_size = is64 ? le16(sb + 0xFE) : 32;
  if (E.desc_size < 32) E.desc_size = 32;
  if (!E.blocks_per_group || !E.inodes_per_group || E.inode_size < 128 || E.inode_size > E.bs ||
      (E.inode_size & (E.inode_size - 1)) || E.desc_size > 1024 || E.first_data >= E.blocks_count)
    return -EINVAL;
  if (E.incompat & ~INCOMPAT_SUPPORTED) {
    ufs_log("ext4fsd: %s: unsupported features %#x", E.dev, E.incompat & ~INCOMPAT_SUPPORTED);
    return -EINVAL;
  }
  if (E.incompat & INCOMPAT_META_BG) {
    ufs_log("ext4fsd: %s: meta_bg layout is not supported", E.dev);
    return -EINVAL;
  }
  if (E.incompat & INCOMPAT_RECOVER)
    ufs_log("ext4fsd: %s: journal needs recovery; serving the last checkpointed state", E.dev);
  E.ngroups = (uint32_t)((E.blocks_count - E.first_data + E.blocks_per_group - 1) / E.blocks_per_group);
  if ((uint64_t)E.ngroups * E.inodes_per_group < E.inodes_count) return -EINVAL;
  off_t devsize = lseek(E.fd, 0, SEEK_END);
  if (devsize > 0 && (uint64_t)devsize < E.blocks_count * E.bs) {
    ufs_log("ext4fsd: %s: filesystem larger than device", E.dev);
    return -EINVAL;
  }
  size_t gdt_len = (size_t)E.ngroups * E.desc_size;
  if (gdt_len > 64u << 20) return -EINVAL;
  E.gdt = malloc(gdt_len);
  if (!E.gdt) return -ENOMEM;
  return dev_read((uint64_t)(E.first_data + 1) * E.bs, E.gdt, gdt_len);
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: ext4fsd <device> <mountpoint>\n");
    return 2;
  }
  E.dev = argv[1];
  E.fd = open(E.dev, O_RDONLY | O_CLOEXEC);
  if (E.fd < 0) {
    ufs_log("ext4fsd: %s: %s", E.dev, strerror(errno));
    return UFS_EXIT_NOT_APPLICABLE;
  }
  int r = ext4_open();
  if (r) {
    ufs_log("ext4fsd: %s: no usable ext2/3/4 filesystem (%s)", E.dev, strerror(-r));
    return UFS_EXIT_NOT_APPLICABLE;
  }
  struct inode root;
  if ((r = read_inode(ROOT_INO, &root)) || !S_ISDIR(root.mode)) {
    ufs_log("ext4fsd: %s: bad root directory", E.dev);
    return UFS_EXIT_NOT_APPLICABLE;
  }
  ufs_log("ext4fsd: %s: %u-byte blocks, %llu blocks, %u groups (read-only)", E.dev, E.bs,
          (unsigned long long)E.blocks_count, E.ngroups);
  return ufs_serve(E.dev, argv[2], &ops, NULL, true);
}
