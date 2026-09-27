/*
 * Block device layer: device registry, MBR/GPT partitions, a write-back
 * buffer cache (4 KiB blocks, LRU, periodic flusher) and the block-special
 * file operations behind /dev/vda, /dev/mmcblk0p1, ...
 *
 * Durability: dirty blocks are written back within FLUSH_INTERVAL seconds,
 * on sync()/fsync() and on unmount/reboot; fsync also flushes the device's
 * volatile write cache.
 */
#include <olux/blkdev.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/uaccess.h>

#define FLUSH_INTERVAL_S 5
#define BCACHE_HASH 1024

static LIST_HEAD(disks);

struct bbuf {
  struct blkdev *disk; /* always the whole disk */
  u64 block;           /* in BCACHE_BLOCK units from the disk start */
  u8 *data;
  bool valid, dirty;
  u64 dirtied_at;
  int users;
  struct list_head lru;  /* most recently used at the head */
  struct list_head hash;
};

static struct list_head hash_table[BCACHE_HASH];
static LIST_HEAD(lru);
static struct mutex cache_lock;
static u64 nbufs, max_bufs, ndirty;
static bool cache_ready;

static void cache_init(void) {
  if (cache_ready) return;
  for (int i = 0; i < BCACHE_HASH; i++) list_init(&hash_table[i]);
  mutex_init(&cache_lock);
  max_bufs = MAX(nr_total_pages() / 16, 256UL); /* up to 1/16 of RAM */
  cache_ready = true;
}

static unsigned hkey(struct blkdev *d, u64 b) { return (unsigned)(((u64)d >> 6) ^ b) % BCACHE_HASH; }

static int disk_io(struct blkdev *d, u64 block, u8 *data, bool write) {
  u64 sector = block * (BCACHE_BLOCK / SECTOR_SIZE);
  u32 count = BCACHE_BLOCK / SECTOR_SIZE;
  if (sector >= d->nr_sectors) return -EIO;
  if (sector + count > d->nr_sectors) count = (u32)(d->nr_sectors - sector);
  mutex_lock(&d->io_lock);
  int r = d->ops->rw(d, sector, data, count, write);
  mutex_unlock(&d->io_lock);
  if (write) {
    d->writes++;
    d->written_sectors += count;
  } else {
    d->reads++;
    d->read_sectors += count;
  }
  if (r) {
    d->errors++;
    pr_err("%s: %s error at sector %llu: %d\n", d->name, write ? "write" : "read", (unsigned long long)sector, r);
  }
  return r;
}

static int writeback(struct bbuf *b) {
  if (!b->dirty) return 0;
  int r = disk_io(b->disk, b->block, b->data, true);
  if (!r) {
    b->dirty = false;
    ndirty--;
  }
  return r;
}

/* Returns a referenced buffer (caller holds cache_lock). */
static struct bbuf *getblk(struct blkdev *d, u64 block, bool read) {
  struct list_head *h = &hash_table[hkey(d, block)];
  struct bbuf *b;
  list_for_each_entry(b, h, hash) {
    if (b->disk == d && b->block == block) {
      list_del(&b->lru);
      list_add(&b->lru, &lru);
      goto found;
    }
  }
  if (nbufs >= max_bufs) {
    /* recycle the least recently used unreferenced buffer */
    struct bbuf *victim = NULL;
    for (struct list_head *p = lru.prev; p != &lru; p = p->prev) {
      struct bbuf *c = list_entry(p, struct bbuf, lru);
      if (!c->users) {
        victim = c;
        break;
      }
    }
    if (victim) {
      if (writeback(victim)) return NULL;
      list_del(&victim->hash);
      list_del(&victim->lru);
      b = victim;
      nbufs--;
    } else {
      b = NULL;
    }
  } else {
    b = NULL;
  }
  if (!b) {
    b = kzalloc(sizeof(*b), 0);
    if (!b) return NULL;
    b->data = get_free_page(0);
    if (!b->data) {
      kfree(b);
      return NULL;
    }
  }
  b->disk = d;
  b->block = block;
  b->valid = b->dirty = false;
  b->users = 0;
  list_add(&b->hash, h);
  list_add(&b->lru, &lru);
  nbufs++;
found:
  if (read && !b->valid) {
    if (disk_io(d, block, b->data, false)) return NULL;
    b->valid = true;
  }
  b->users++;
  return b;
}

static ssize_t cached_rw(struct blkdev *bd, void *buf, size_t n, u64 off, bool write) {
  struct blkdev *d = bd->whole;
  u64 limit = bd->nr_sectors * SECTOR_SIZE;
  if (off >= limit) return 0;
  if (n > limit - off) n = limit - off;
  if (write && d->readonly) return -EROFS;
  u64 base = bd->start * SECTOR_SIZE + off;
  size_t done = 0;
  mutex_lock(&cache_lock);
  while (done < n) {
    u64 pos = base + done;
    u64 block = pos / BCACHE_BLOCK;
    size_t bo = pos % BCACHE_BLOCK;
    size_t chunk = MIN(n - done, (size_t)BCACHE_BLOCK - bo);
    /* a full-block overwrite does not need the old contents */
    struct bbuf *b = getblk(d, block, !(write && chunk == BCACHE_BLOCK));
    if (!b) {
      mutex_unlock(&cache_lock);
      return done ? (ssize_t)done : -EIO;
    }
    if (write) {
      memcpy(b->data + bo, (u8 *)buf + done, chunk);
      b->valid = true;
      if (!b->dirty) {
        b->dirty = true;
        b->dirtied_at = ktime_ns();
        ndirty++;
      }
    } else {
      memcpy((u8 *)buf + done, b->data + bo, chunk);
    }
    b->users--;
    done += chunk;
  }
  mutex_unlock(&cache_lock);
  return done;
}

ssize_t bdev_read(struct blkdev *bd, void *buf, size_t n, u64 off) { return cached_rw(bd, buf, n, off, false); }
ssize_t bdev_write(struct blkdev *bd, const void *buf, size_t n, u64 off) {
  return cached_rw(bd, (void *)buf, n, off, true);
}

static int sync_disk(struct blkdev *d, u64 older_than) {
  int err = 0;
  mutex_lock(&cache_lock);
  struct bbuf *b;
  list_for_each_entry(b, &lru, lru) {
    if (b->dirty && (!d || b->disk == d) && (!older_than || b->dirtied_at <= older_than)) {
      int r = writeback(b);
      if (r) err = r;
    }
  }
  mutex_unlock(&cache_lock);
  return err;
}

int bdev_sync(struct blkdev *bd) {
  struct blkdev *d = bd->whole;
  int r = sync_disk(d, 0);
  if (d->ops->flush) {
    mutex_lock(&d->io_lock);
    int f = d->ops->flush(d);
    mutex_unlock(&d->io_lock);
    if (!r) r = f;
  }
  return r;
}

void bcache_sync_all(void) {
  if (!cache_ready) return;
  struct blkdev *d;
  list_for_each_entry(d, &disks, link)
    if (d->whole == d) bdev_sync(d);
}

u64 bcache_dirty_blocks(void) { return ndirty; }
u64 bcache_bytes(void) { return nbufs * BCACHE_BLOCK; }

static int flusher(void *arg) {
  for (;;) {
    msleep(1000);
    lock_kernel();
    u64 now = ktime_ns();
    if (ndirty && now > FLUSH_INTERVAL_S * NSEC_PER_SEC) sync_disk(NULL, now - FLUSH_INTERVAL_S * NSEC_PER_SEC);
    unlock_kernel();
  }
  return 0;
}

/* ---------------- registry and partitions ---------------- */

struct blkdev *blkdev_lookup(dev_t dev) {
  struct blkdev *d;
  list_for_each_entry(d, &disks, link)
    if (d->dev == dev) return d;
  return NULL;
}

struct blkdev *blkdev_by_name(const char *name) {
  struct blkdev *d;
  list_for_each_entry(d, &disks, link)
    if (!strcmp(d->name, name)) return d;
  return NULL;
}

static struct blkdev *add_partition(struct blkdev *disk, int n, u64 start, u64 len, const char *sep) {
  if (start >= disk->nr_sectors || len == 0) return NULL;
  if (start + len > disk->nr_sectors) len = disk->nr_sectors - start;
  struct blkdev *p = kzalloc(sizeof(*p), 0);
  if (!p) return NULL;
  snprintf(p->name, sizeof(p->name), "%s%s%d", disk->name, sep, n);
  p->dev = disk->dev + n;
  p->nr_sectors = len;
  p->readonly = disk->readonly;
  p->ops = disk->ops;
  p->priv = disk->priv;
  p->whole = disk;
  p->start = start;
  list_add_tail(&p->link, &disks);
  devfs_create(p->name, S_IFBLK | 0660, p->dev);
  pr_info("  %s: start %llu, %llu MiB\n", p->name, (unsigned long long)start, (unsigned long long)(len >> 11));
  return p;
}

struct gpt_header {
  char sig[8];
  u32 rev, hsize, hcrc, res;
  u64 cur, backup, first, last;
  u8 guid[16];
  u64 entries_lba;
  u32 nentries, entry_size, entries_crc;
};

static void scan_partitions(struct blkdev *d, const char *sep) {
  u8 *buf = kmalloc(SECTOR_SIZE * 2, 0);
  if (!buf) return;
  if (bdev_read(d, buf, SECTOR_SIZE * 2, 0) != SECTOR_SIZE * 2 || buf[510] != 0x55 || buf[511] != 0xaa) {
    kfree(buf);
    return;
  }
  bool protective = buf[446 + 4] == 0xee;
  struct gpt_header *g = (struct gpt_header *)(buf + SECTOR_SIZE);
  if (protective && !memcmp(g->sig, "EFI PART", 8) && g->entry_size >= 128 && g->nentries <= 256) {
    u32 esz = g->entry_size, cnt = g->nentries;
    u64 lba = g->entries_lba;
    u8 *e = kmalloc(esz, 0);
    for (u32 i = 0, n = 1; e && i < cnt; i++) {
      if (bdev_read(d, e, esz, lba * SECTOR_SIZE + (u64)i * esz) != (ssize_t)esz) break;
      bool used = false;
      for (int k = 0; k < 16; k++) used |= e[k] != 0;
      if (!used) continue;
      u64 first = *(u64 *)(e + 32), last = *(u64 *)(e + 40);
      if (last >= first) add_partition(d, n, first, last - first + 1, sep);
      n++;
    }
    kfree(e);
  } else {
    for (int i = 0; i < 4; i++) {
      u8 *p = buf + 446 + i * 16;
      u8 type = p[4];
      u32 start = p[8] | p[9] << 8 | p[10] << 16 | (u32)p[11] << 24;
      u32 len = p[12] | p[13] << 8 | p[14] << 16 | (u32)p[15] << 24;
      if (type && type != 0x05 && type != 0x0f && len) add_partition(d, i + 1, start, len, sep);
    }
  }
  kfree(buf);
}

int blkdev_register(struct blkdev *d, u32 major, const char *sep) {
  cache_init();
  static int minor_base[256];
  d->dev = MKDEV(major, minor_base[major & 0xff]);
  minor_base[major & 0xff] += 16;
  d->whole = d;
  d->start = 0;
  mutex_init(&d->io_lock);
  list_add_tail(&d->link, &disks);
  devfs_create(d->name, S_IFBLK | 0660, d->dev);
  pr_info("%s: %llu MiB%s\n", d->name, (unsigned long long)(d->nr_sectors >> 11), d->readonly ? " (read-only)" : "");
  scan_partitions(d, sep);
  static bool flusher_started;
  if (!flusher_started) {
    struct thread *t = kthread_create(flusher, NULL, "bflush");
    if (t) sched_add_new(t);
    flusher_started = true;
  }
  return 0;
}

int blkdev_stats(char *buf, size_t size) {
  size_t n = 0;
  struct blkdev *d;
  list_for_each_entry(d, &disks, link) {
    n += snprintf(buf + n, n < size ? size - n : 0, "%4u %7u %s %llu %llu %llu %llu %llu\n", MAJOR(d->dev),
                  MINOR(d->dev), d->name, (unsigned long long)d->reads, (unsigned long long)d->read_sectors,
                  (unsigned long long)d->writes, (unsigned long long)d->written_sectors,
                  (unsigned long long)d->errors);
  }
  return (int)MIN(n, size);
}

/* ---------------- block special files ---------------- */

#define BLKROSET 0x125d
#define BLKROGET 0x125e
#define BLKGETSIZE 0x1260
#define BLKFLSBUF 0x1261
#define BLKSSZGET 0x1268
#define BLKGETSIZE64 0x80081272

static struct blkdev *file_bdev(struct file *f) { return f->priv; }

static int bdev_open(struct inode *i, struct file *f) {
  struct blkdev *d = blkdev_lookup(i->rdev);
  if (!d) return -ENXIO;
  if ((f->mode & FMODE_WRITE) && d->readonly) return -EROFS;
  f->priv = d;
  return 0;
}

static ssize_t bdev_file_read(struct file *f, struct iobuf *b, loff_t *pos) {
  struct blkdev *d = file_bdev(f);
  u8 *tmp = kmalloc(BCACHE_BLOCK, 0);
  if (!tmp) return -ENOMEM;
  size_t done = 0;
  while (done < b->len) {
    size_t n = MIN(b->len - done, (size_t)BCACHE_BLOCK);
    ssize_t r = bdev_read(d, tmp, n, *pos);
    if (r <= 0) {
      if (!done && r < 0) done = r;
      break;
    }
    if (iob_write(b, done, tmp, r)) {
      if (!done) done = -EFAULT;
      break;
    }
    done += r;
    *pos += r;
    if ((size_t)r < n) break;
  }
  kfree(tmp);
  return done;
}

static ssize_t bdev_file_write(struct file *f, struct iobuf *b, loff_t *pos) {
  struct blkdev *d = file_bdev(f);
  u8 *tmp = kmalloc(BCACHE_BLOCK, 0);
  if (!tmp) return -ENOMEM;
  size_t done = 0;
  while (done < b->len) {
    size_t n = MIN(b->len - done, (size_t)BCACHE_BLOCK);
    if (iob_read(b, done, tmp, n)) {
      if (!done) done = -EFAULT;
      break;
    }
    ssize_t r = bdev_write(d, tmp, n, *pos);
    if (r <= 0) {
      if (!done) done = r ? r : -ENOSPC;
      break;
    }
    done += r;
    *pos += r;
    if ((size_t)r < n) break;
  }
  kfree(tmp);
  if (done > 0 && (f->flags & O_SYNC) == O_SYNC) bdev_sync(d);
  return done;
}

static loff_t bdev_llseek(struct file *f, loff_t off, int whence) {
  struct blkdev *d = file_bdev(f);
  loff_t size = (loff_t)(d->nr_sectors * SECTOR_SIZE);
  loff_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos : whence == SEEK_END ? size : -1;
  if (base < 0 || base + off < 0) return -EINVAL;
  return f->pos = base + off;
}

static long bdev_ioctl(struct file *f, unsigned cmd, u64 arg) {
  struct blkdev *d = file_bdev(f);
  switch (cmd) {
    case BLKGETSIZE64:
      return put_user((u64)(d->nr_sectors * SECTOR_SIZE), arg);
    case BLKGETSIZE:
      return put_user((u64)d->nr_sectors, arg);
    case BLKSSZGET:
      return put_user((s32)SECTOR_SIZE, arg);
    case BLKROGET:
      return put_user((s32)d->readonly, arg);
    case BLKFLSBUF:
      return bdev_sync(d);
    default:
      return -ENOTTY;
  }
}

static int bdev_fsync(struct file *f) { return bdev_sync(file_bdev(f)); }

const struct file_operations blkdev_fops = {
    .open = bdev_open,
    .read = bdev_file_read,
    .write = bdev_file_write,
    .llseek = bdev_llseek,
    .ioctl = bdev_ioctl,
    .fsync = bdev_fsync,
};
