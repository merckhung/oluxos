/* Unpack a "newc" cpio archive (the initramfs) into the root filesystem. */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>

static bool hex8(const char *s, u32 *out) {
  u32 v = 0;
  for (int i = 0; i < 8; i++) {
    char c = s[i];
    int d = isdigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (d < 0) return false;
    v = (v << 4) | d;
  }
  *out = v;
  return true;
}

int unpack_initramfs(const void *data, size_t size) {
  const u8 *p = data, *end = p + size;
  int files = 0;
  while (p + 110 <= end) {
    if (memcmp(p, "070701", 6) && memcmp(p, "070702", 6)) {
      /* allow zero padding between concatenated archives */
      if (*p == 0) {
        p++;
        continue;
      }
      pr_err("initramfs: bad magic at offset %zu\n", (size_t)(p - (const u8 *)data));
      return -EINVAL;
    }
    u32 f[13];
    for (int i = 0; i < 13; i++)
      if (!hex8((const char *)p + 6 + i * 8, &f[i])) return -EINVAL;
    u32 mode = f[1], uid = f[2], gid = f[3], mtime = f[5], filesize = f[6];
    u32 rdevmajor = f[9], rdevminor = f[10], namesize = f[11];
    const char *name = (const char *)p + 110;
    if ((const u8 *)name + namesize > end || namesize == 0 || name[namesize - 1]) return -EINVAL;
    const u8 *body = p + ALIGN_UP(110 + namesize, 4);
    if (body + filesize > end) return -EINVAL;
    if (!strcmp(name, "TRAILER!!!")) break;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/%s", name[0] == '.' && name[1] == '/' ? name + 2 : name);
    int r = 0;
    if (!strcmp(path, "/.") || !strcmp(path, "/")) {
      r = 0;
    } else if (S_ISDIR(mode)) {
      r = vfs_mkdir(AT_FDCWD, path, mode & 07777);
      if (r == -EEXIST) r = 0;
    } else if (S_ISREG(mode)) {
      struct file *fl = kernel_open(path, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
      if (IS_ERR(fl)) r = (int)PTR_ERR(fl);
      else {
        ssize_t w = kernel_pwrite(fl, body, filesize, 0);
        if (w != (ssize_t)filesize) r = w < 0 ? (int)w : -EIO;
        file_put(fl);
      }
    } else if (S_ISLNK(mode)) {
      char target[PATH_MAX];
      size_t n = MIN((size_t)filesize, sizeof(target) - 1);
      memcpy(target, body, n);
      target[n] = '\0';
      r = vfs_symlink(target, AT_FDCWD, path);
    } else if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode) || S_ISSOCK(mode)) {
      r = vfs_mknod(AT_FDCWD, path, mode, MKDEV(rdevmajor, rdevminor));
    }
    if (r && r != -EEXIST) pr_warn("initramfs: %s: error %d\n", path, r);
    /* apply exact mode/owner/time (umask must not apply) */
    struct path pp;
    if (!r && !kern_path(path, 0, &pp)) {
      struct inode *ino = pp.dentry->inode;
      ino->mode = (ino->mode & S_IFMT) | (mode & 07777);
      ino->uid = uid;
      ino->gid = gid;
      ino->mtime.tv_sec = ino->ctime.tv_sec = ino->atime.tv_sec = mtime;
      path_put(&pp);
    }
    files++;
    p = body + ALIGN_UP(filesize, 4);
  }
  pr_info("initramfs: unpacked %d entries\n", files);
  return 0;
}
