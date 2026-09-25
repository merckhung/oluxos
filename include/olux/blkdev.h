#ifndef OLUX_BLKDEV_H
#define OLUX_BLKDEV_H

#include <olux/list.h>
#include <olux/types.h>
#include <olux/wait.h>

#define SECTOR_SIZE 512
#define BCACHE_BLOCK 4096 /* buffer cache granularity */

struct blkdev;

struct blkdev_ops {
  /* Synchronous transfer of `count` sectors; may sleep. Returns 0 or -errno. */
  int (*rw)(struct blkdev *bd, u64 sector, void *buf, u32 count, bool write);
  int (*flush)(struct blkdev *bd); /* write cache flush, may be NULL */
};

struct blkdev {
  char name[16];
  dev_t dev;
  u64 nr_sectors;
  bool readonly;
  const struct blkdev_ops *ops;
  void *priv;
  struct blkdev *whole;   /* parent disk for partitions, else self */
  u64 start;              /* first sector (partitions) */
  int nparts;
  struct mutex io_lock;   /* one request at a time per disk */
  struct list_head link;
  /* statistics */
  u64 reads, writes, read_sectors, written_sectors, errors;
};

/* Register a whole disk: creates /dev/<name> and partitions /dev/<name><p>N. */
int blkdev_register(struct blkdev *bd, u32 major, const char *part_sep);
struct blkdev *blkdev_lookup(dev_t dev);
struct blkdev *blkdev_by_name(const char *name);

/* Cached block I/O at byte granularity (partition-relative). */
ssize_t bdev_read(struct blkdev *bd, void *buf, size_t n, u64 off);
ssize_t bdev_write(struct blkdev *bd, const void *buf, size_t n, u64 off);
int bdev_sync(struct blkdev *bd);
void bcache_sync_all(void);
u64 bcache_dirty_blocks(void);
int blkdev_stats(char *buf, size_t size);

#endif
