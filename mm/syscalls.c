/* Memory management system calls. */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/uaccess.h>
#include <olux/vm.h>

static struct mm *cur_mm(void) { return current->proc->mm; }

long sys_brk(u64 addr);
long sys_brk(u64 addr) { return do_brk(cur_mm(), addr); }

long sys_mmap(u64 addr, u64 len, u64 prot, u64 flags, u64 fd, u64 off);
long sys_mmap(u64 addr, u64 len, u64 prot, u64 flags, u64 fd, u64 off) {
  if (prot & ~(u64)(PROT_READ | PROT_WRITE | PROT_EXEC)) return -EINVAL;
  struct file *f = NULL;
  if (!(flags & MAP_ANONYMOUS)) {
    f = fget((int)fd);
    if (!f) return -EBADF;
    if (!(f->mode & FMODE_READ)) {
      file_put(f);
      return -EACCES;
    }
    if ((flags & MAP_TYPE) == MAP_SHARED && (prot & PROT_WRITE) && !(f->mode & FMODE_WRITE)) {
      file_put(f);
      return -EACCES;
    }
    if ((prot & PROT_EXEC) && f->path.mnt && (f->path.mnt->sb->flags & SB_NOEXEC)) {
      file_put(f);
      return -EPERM;
    }
  }
  long r = do_mmap(cur_mm(), addr, len, (int)prot, (int)flags, f, off);
  if (f) file_put(f);
  return r;
}

long sys_munmap(u64 addr, u64 len);
long sys_munmap(u64 addr, u64 len) { return do_munmap(cur_mm(), addr, len); }

long sys_mprotect(u64 addr, u64 len, u64 prot);
long sys_mprotect(u64 addr, u64 len, u64 prot) {
  if (prot & ~(u64)(PROT_READ | PROT_WRITE | PROT_EXEC)) return -EINVAL;
  return do_mprotect(cur_mm(), addr, len, (int)prot);
}

long sys_mremap(u64 old, u64 old_len, u64 new_len, u64 flags, u64 new_addr);
long sys_mremap(u64 old, u64 old_len, u64 new_len, u64 flags, u64 new_addr) {
  return do_mremap(cur_mm(), old, old_len, new_len, (int)flags, new_addr);
}

long sys_madvise(u64 addr, u64 len, u64 advice);
long sys_madvise(u64 addr, u64 len, u64 advice) {
  if (advice == 4 /* MADV_DONTNEED */) {
    /* drop private anonymous pages: remap them as fresh zero pages */
    struct mm *mm = cur_mm();
    struct vma *v = vma_find(mm, addr);
    if (!v) return -ENOMEM;
    if (v->file || (v->flags & (VM_IO | VM_SHARED))) return 0;
    u32 flags = v->flags;
    u64 end = addr + ALIGN_UP(len, PAGE_SIZE);
    if (end > v->end) end = v->end;
    int prot = ((flags & VM_READ) ? PROT_READ : 0) | ((flags & VM_WRITE) ? PROT_WRITE : 0) |
               ((flags & VM_EXEC) ? PROT_EXEC : 0);
    long r = do_mmap(mm, addr, end - addr, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, NULL, 0);
    if (r >= 0 && (flags & VM_GROWSDOWN)) {
      struct vma *nv = vma_find(mm, addr);
      if (nv) nv->flags |= VM_GROWSDOWN;
    }
    return r < 0 ? r : 0;
  }
  return 0;
}

long sys_msync(u64 addr, u64 len, u64 flags);
long sys_msync(u64 addr, u64 len, u64 flags) { return 0; }
long sys_mlock(u64 addr, u64 len);
long sys_mlock(u64 addr, u64 len) { return mm_populate(cur_mm(), addr, len, false) ? -ENOMEM : 0; }
long sys_munlock(u64 addr, u64 len);
long sys_munlock(u64 addr, u64 len) { return 0; }
long sys_mlockall(u64 flags);
long sys_mlockall(u64 flags) { return 0; }
long sys_munlockall(void);
long sys_munlockall(void) { return 0; }

long sys_mincore(u64 addr, u64 len, u64 vec);
long sys_mincore(u64 addr, u64 len, u64 vec) {
  if (addr & ~PAGE_MASK) return -EINVAL;
  u64 pages = DIV_ROUND_UP(len, PAGE_SIZE);
  for (u64 i = 0; i < pages; i++) {
    if (!vma_find(cur_mm(), addr + i * PAGE_SIZE)) return -ENOMEM;
    u8 one = 1;
    if (copy_to_user(vec + i, &one, 1)) return -EFAULT;
  }
  return 0;
}
