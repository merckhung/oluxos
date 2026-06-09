#include <arm64/platform.h>
#include <arm64/task.h>
#include <driver/arm_timer.h>
#include <driver/gicv2.h>
#include <types.h>
#include <driver/fb.h>
#include <arm64/kdbger.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>
#include <kernel/vmm.h>
#include <clib.h>

extern char _stack_top[];

#if CONFIG_BOARD_RPI4
#define UART_IRQ 153
#else
#define UART_IRQ 33
#endif


// Include generated userspace binary
#if CONFIG_BOARD_RPI4
#include "user_shell_bin_rpi4.h"
#else
#include "user_shell_bin.h"
#endif

#include <kernel/console.h>

void pl011_init(void);
void pl011_puts(const char* s);
void pl011_putc(char c);
char pl011_getc(void);
extern void thread_exit(void);

void kputs(const char* s) {
  pl011_puts(s);
}

void print_hex(uint64_t val) {
  char hex[17];
  int i;
  for (i = 15; i >= 0; i--) {
    int digit = val & 0xF;
    hex[i] = digit < 10 ? '0' + digit : 'A' + digit - 10;
    val >>= 4;
  }
  hex[16] = '\0';
  pl011_puts("0x");
  pl011_puts(hex);
}

int copy_string_from_user(char* dest, uint64_t user_src, int max_len) {
  extern uint64_t translate_user_va(Thread* t, uint64_t va);
  int len = 0;
  while (len < max_len - 1) {
    uint64_t phys = translate_user_va(current_thread, user_src + len);
    if (!phys) return -1;
    char c = *(char*)phys;
    dest[len++] = c;
    if (c == '\0') break;
  }
  dest[len] = '\0';
  return len;
}

int copy_to_user(uint64_t user_dest, const void* src, int len) {
  extern uint64_t translate_user_va(Thread* t, uint64_t va);
  int copied = 0;
  while (copied < len) {
    uint64_t va = user_dest + copied;
    uint64_t phys = translate_user_va(current_thread, va);
    if (!phys) return -1;
    uint64_t page_offset = va & 0xFFF;
    uint64_t chunk = 4096 - page_offset;
    if (chunk > (len - copied)) chunk = len - copied;
    CbMemCpy((void*)phys, (const char*)src + copied, chunk);
    copied += chunk;
  }
  return 0;
}

int copy_from_user(void* dest, uint64_t user_src, int len) {
  extern uint64_t translate_user_va(Thread* t, uint64_t va);
  int copied = 0;
  while (copied < len) {
    uint64_t va = user_src + copied;
    uint64_t phys = translate_user_va(current_thread, va);
    if (!phys) return -1;
    uint64_t page_offset = va & 0xFFF;
    uint64_t chunk = 4096 - page_offset;
    if (chunk > (len - copied)) chunk = len - copied;
    CbMemCpy((char*)dest + copied, (const void*)phys, chunk);
    copied += chunk;
  }
  return 0;
}

int is_root_dir(const char* path) {
  if (path[0] == '/' && path[1] == '\0') return 1;
  if (path[0] == '.' && path[1] == '\0') return 1;
  if (path[0] == '.' && path[1] == '.' && path[2] == '\0') return 1;
  return 0;
}

void exception_handler_dump(uint64_t lr, const char* msg) {
  pl011_puts("\n!!! EXCEPTION !!!\n");
  pl011_puts(msg);
  pl011_puts("\nLR: ");
  print_hex(lr);
  pl011_puts("\n");

  uint64_t esr, far, elr;
  __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
  __asm__ volatile("mrs %0, far_el1" : "=r"(far));
  __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));

  pl011_puts("ELR_EL1: ");
  print_hex(elr);
  pl011_puts("\nESR_EL1: ");
  print_hex(esr);
  pl011_puts("\nFAR_EL1: ");
  print_hex(far);
  pl011_puts("\n");

  while (1);
}

void irq_handler_el1(ARM64Registers* regs) {
  uint32_t irq = gicv2_acknowledge_irq();

  if (irq == 30) {
    arm_timer_reset(100);
    gicv2_end_of_irq(irq);
  } else if (irq == UART_IRQ) {
    kdbger_intr_handler();
    gicv2_end_of_irq(irq);
  } else {
    pl011_puts("Unexpected EL1 IRQ: ");
    print_hex(irq);
    pl011_puts("\n");
    gicv2_end_of_irq(irq);
  }
}

void irq_handler_el0(ARM64Registers* regs) {
  uint32_t irq = gicv2_acknowledge_irq();

  if (irq == 30) {
    arm_timer_reset(100);
    thread_set_current_regs(regs);
    gicv2_end_of_irq(irq);  // EOI before schedule
    schedule();
  } else if (irq == UART_IRQ) {
    kdbger_intr_handler();
    gicv2_end_of_irq(irq);
  } else {
    pl011_puts("Unexpected EL0 IRQ: ");
    print_hex(irq);
    pl011_puts("\n");
    exception_handler_dump(regs->pc, "EL0 IRQ Fault");
    gicv2_end_of_irq(irq);
  }
}

#define FS_SERVER_TID 3

struct FsOpenReq {
  unsigned int sender;
  unsigned int cmd;
  char filename[64];
};

struct FsCloseReq {
  unsigned int sender;
  unsigned int cmd;
  unsigned int handle;
};

struct FsReadReq {
  unsigned int sender;
  unsigned int cmd;
  struct {
    unsigned int handle;
    unsigned int count;
  } read;
};

struct FsReply {
  int size;
  unsigned char data[512];
};

int copy_string_from_user(char* dest, uint64_t user_src, int max_len);
int copy_to_user(uint64_t user_dest, const void* src, int len);
int copy_from_user(void* dest, uint64_t user_src, int len);

int krn_openat(ARM64Registers* regs, int dfd, const char* user_filename, int flags) {
  char filename[64];
  if (copy_string_from_user(filename, (uint64_t)user_filename, 64) < 0) {
    pl011_puts("krn_openat: copy_string_from_user failed\n");
    return -2; // -ENOENT
  }

  pl011_puts("krn_openat: filename=\"");
  pl011_puts(filename);
  pl011_puts("\" dfd=");
  print_hex(dfd);
  pl011_puts("\n");

  int fd;
  for (fd = 3; fd < MAX_KERNEL_FDS; fd++) {
    if (!current_thread->fds[fd].used) break;
  }
  if (fd == MAX_KERNEL_FDS) {
    pl011_puts("krn_openat: EMFILE\n");
    return -24; // -EMFILE
  }

  extern int is_root_dir(const char* path);
  if (is_root_dir(filename)) {
    current_thread->fds[fd].used = 1;
    current_thread->fds[fd].srv_tid = FS_SERVER_TID;
    current_thread->fds[fd].handle = 999;
    current_thread->fds[fd].offset = 0;
    current_thread->fds[fd].path[0] = '/';
    current_thread->fds[fd].path[1] = '\0';
    pl011_puts("krn_openat: root dir mapped to fd=");
    print_hex(fd);
    pl011_puts("\n");
    return fd;
  }

  struct FsOpenReq req;
  req.sender = current_thread->tid;
  req.cmd = 2; // Open File
  int len = 0;
  while (filename[len] && len < 63) {
    req.filename[len] = filename[len];
    len++;
  }
  req.filename[len] = '\0';

  extern int thread_ipc_send(uint32_t dest, void* buf, uint32_t size);
  extern int thread_ipc_recv(uint32_t src, void* buf, uint32_t size);

  int ret = thread_ipc_send(FS_SERVER_TID, &req, sizeof(req));
  if (ret < 0 && ret != IPC_BLOCKED) {
    pl011_puts("krn_openat: ipc_send failed\n");
    return -5; // -EIO
  }

  struct FsReply reply;
  ret = thread_ipc_recv(FS_SERVER_TID, &reply, sizeof(reply));
  int n;
  if (ret == IPC_BLOCKED) {
    n = regs->x[0];
  } else {
    n = ret;
  }

  if (n >= 4) {
    int handle = reply.size;
    if (handle >= 0) {
      current_thread->fds[fd].used = 1;
      current_thread->fds[fd].srv_tid = FS_SERVER_TID;
      current_thread->fds[fd].handle = handle;
      current_thread->fds[fd].offset = 0;
      int p_len = 0;
      while (filename[p_len] && p_len < 63) {
        current_thread->fds[fd].path[p_len] = filename[p_len];
        p_len++;
      }
      current_thread->fds[fd].path[p_len] = '\0';
      pl011_puts("krn_openat: success fd=");
      print_hex(fd);
      pl011_puts(" handle=");
      print_hex(handle);
      pl011_puts("\n");
      return fd;
    } else {
      pl011_puts("krn_openat: FS server returned error=");
      print_hex(handle);
      pl011_puts("\n");
      return handle; // Error code from server
    }
  }
  pl011_puts("krn_openat: recv failed\n");
  return -5; // -EIO
}

int krn_close(ARM64Registers* regs, int fd) {
  if (fd < 0 || fd >= MAX_KERNEL_FDS) return -9; // -EBADF
  if (fd < 3) return 0; // stdin/stdout/stderr are closed successfully (nop)
  if (!current_thread->fds[fd].used) return -9; // -EBADF
  
  if (current_thread->fds[fd].handle == 999) {
    current_thread->fds[fd].used = 0;
    return 0;
  }

  struct FsCloseReq req;
  req.sender = current_thread->tid;
  req.cmd = 4; // Close File
  req.handle = current_thread->fds[fd].handle;

  extern int thread_ipc_send(uint32_t dest, void* buf, uint32_t size);
  extern int thread_ipc_recv(uint32_t src, void* buf, uint32_t size);

  int ret = thread_ipc_send(FS_SERVER_TID, &req, sizeof(req));
  if (ret < 0 && ret != IPC_BLOCKED) {
    return -5; // -EIO
  }

  struct FsReply reply;
  ret = thread_ipc_recv(FS_SERVER_TID, &reply, sizeof(reply));
  int n;
  if (ret == IPC_BLOCKED) {
    n = regs->x[0];
  } else {
    n = ret;
  }

  current_thread->fds[fd].used = 0; // Free it anyway

  if (n >= 4) {
    return reply.size; // Status from server
  }
  return -5; // -EIO
}

int krn_read(ARM64Registers* regs, int fd, void* user_buf, size_t count) {
  if (fd < 0 || fd >= MAX_KERNEL_FDS) return -9; // -EBADF
  
  if (fd == 0) { // stdin
    char* buf = (char*)user_buf;
    uint64_t len = count;
    if (len > 0) {
      extern int pl011_hasc(void);
      if (pl011_hasc()) {
        uint64_t count_read = 0;
        char c = pl011_getc();
        if (copy_to_user((uint64_t)buf + count_read, &c, 1) < 0) return -14; // -EFAULT
        count_read++;
        while (count_read < len && pl011_hasc()) {
          c = pl011_getc();
          if (copy_to_user((uint64_t)buf + count_read, &c, 1) < 0) return -14; // -EFAULT
          count_read++;
        }
        return count_read;
      } else {
        extern int thread_block_on_uart(void* buf, uint32_t len);
        int ret = thread_block_on_uart(buf, len);
        if (ret != IPC_BLOCKED) {
          return ret;
        }
        return IPC_BLOCKED;
      }
    } else {
      return 0;
    }
  }

  if (!current_thread->fds[fd].used) return -9; // -EBADF

  if (current_thread->fds[fd].handle == 999) {
    return -21; // -EISDIR
  }

  struct FsReadReq req;
  req.sender = current_thread->tid;
  req.cmd = 3; // Read File Handle
  req.read.handle = current_thread->fds[fd].handle;
  req.read.count = count > 512 ? 512 : count;

  extern int thread_ipc_send(uint32_t dest, void* buf, uint32_t size);
  extern int thread_ipc_recv(uint32_t src, void* buf, uint32_t size);

  int ret = thread_ipc_send(FS_SERVER_TID, &req, sizeof(req));
  if (ret < 0 && ret != IPC_BLOCKED) {
    return -5; // -EIO
  }

  struct FsReply reply;
  ret = thread_ipc_recv(FS_SERVER_TID, &reply, sizeof(reply));
  int n;
  if (ret == IPC_BLOCKED) {
    n = regs->x[0];
  } else {
    n = ret;
  }

  if (n >= 4) {
    int bytes_read = reply.size;
    if (bytes_read >= 0) {
      if (copy_to_user((uint64_t)user_buf, reply.data, bytes_read) < 0) {
        return -14; // -EFAULT
      }
      current_thread->fds[fd].offset += bytes_read;
      return bytes_read;
    } else {
      return bytes_read; // Error from server
    }
  }
  return -5; // -EIO
}

int krn_write(ARM64Registers* regs, int fd, const void* user_buf, size_t count) {
  if (fd < 0 || fd >= MAX_KERNEL_FDS) return -9; // -EBADF
  
  if (fd == 1 || fd == 2) { // stdout/stderr
    char buf[128];
    size_t written = 0;
    while (written < count) {
      size_t chunk = count - written;
      if (chunk > 128) chunk = 128;
      if (copy_from_user(buf, (uint64_t)user_buf + written, chunk) < 0) {
        return -14; // -EFAULT
      }
      size_t i;
      for (i = 0; i < chunk; i++) {
        char c = buf[i];
        if (c == '\n') pl011_putc('\r');
        pl011_putc(c);
      }
      written += chunk;
    }
    return written;
  }

  if (!current_thread->fds[fd].used) return -9; // -EBADF
  return -30; // -EROFS (Read-only file system)
}

struct iovec {
  uint64_t iov_base;
  uint64_t iov_len;
};

int krn_writev(ARM64Registers* regs, int fd, const struct iovec* user_iov, int iovcnt) {
  if (fd < 0 || fd >= MAX_KERNEL_FDS) return -9; // -EBADF
  
  struct iovec iov[8];
  if (iovcnt > 8) iovcnt = 8;
  if (copy_from_user(iov, (uint64_t)user_iov, iovcnt * sizeof(struct iovec)) < 0) {
    return -14; // -EFAULT
  }

  int total = 0;
  int i;
  for (i = 0; i < iovcnt; i++) {
    int ret = krn_write(regs, fd, (void*)iov[i].iov_base, iov[i].iov_len);
    if (ret < 0) {
      if (total > 0) return total;
      return ret;
    }
    total += ret;
  }
  return total;
}

struct linux_dirent64 {
  uint64_t        d_ino;
  int64_t         d_off;
  unsigned short  d_reclen;
  unsigned char   d_type;
  char            d_name[];
};

int krn_getdents64(ARM64Registers* regs, int fd, void* user_dirp, size_t count) {
  pl011_puts("krn_getdents64: fd=");
  print_hex(fd);
  pl011_puts(" user_dirp=");
  print_hex((uint64_t)user_dirp);
  pl011_puts(" count=");
  print_hex(count);
  pl011_puts("\n");

  if (fd < 0 || fd >= MAX_KERNEL_FDS) {
    pl011_puts("krn_getdents64: EBADF (fd out of range)\n");
    return -9; // -EBADF
  }
  if (!current_thread->fds[fd].used) {
    pl011_puts("krn_getdents64: EBADF (fd not used)\n");
    return -9; // -EBADF
  }

  struct FsOpenReq req;
  req.sender = current_thread->tid;
  req.cmd = 1; // List Dir
  int len = 0;
  while (current_thread->fds[fd].path[len] && len < 63) {
    req.filename[len] = current_thread->fds[fd].path[len];
    len++;
  }
  req.filename[len] = '\0';

  extern int thread_ipc_send(uint32_t dest, void* buf, uint32_t size);
  extern int thread_ipc_recv(uint32_t src, void* buf, uint32_t size);

  int ret = thread_ipc_send(FS_SERVER_TID, &req, sizeof(req));
  if (ret < 0 && ret != IPC_BLOCKED) {
    pl011_puts("krn_getdents64: ipc_send failed\n");
    return -5; // -EIO
  }

  struct FsReply reply;
  ret = thread_ipc_recv(FS_SERVER_TID, &reply, sizeof(reply));
  int n;
  if (ret == IPC_BLOCKED) {
    n = regs->x[0];
  } else {
    n = ret;
  }

  if (n < 4) {
    pl011_puts("krn_getdents64: ipc_recv returned less than 4 bytes\n");
    return -5; // -EIO
  }
  int read_bytes = reply.size;
  if (read_bytes < 0) {
    pl011_puts("krn_getdents64: FS server returned error=");
    print_hex(read_bytes);
    pl011_puts("\n");
    return read_bytes;
  }

  char* data = (char*)reply.data;
  if (read_bytes < 512) {
    data[read_bytes] = '\0';
  } else {
    data[511] = '\0';
    read_bytes = 511;
  }

  int offset = 0;
  int entry_idx = 0;
  int written = 0;
  uint64_t target_offset = current_thread->fds[fd].offset;

  while (offset < read_bytes) {
    int end = offset;
    while (end < read_bytes && data[end] != '\n') end++;
    int name_len = end - offset;
    if (name_len == 0) {
      offset++;
      continue;
    }

    if (entry_idx >= target_offset) {
      int reclen = (8 + 8 + 2 + 1 + name_len + 1 + 7) & ~7;
      if (written + reclen > count) {
        if (written == 0) {
          pl011_puts("krn_getdents64: EINVAL (count too small)\n");
          return -22; // -EINVAL
        }
        pl011_puts("krn_getdents64: buffer full, stopping\n");
        break;
      }

      char dirent_buf[128];
      struct linux_dirent64* d = (struct linux_dirent64*)dirent_buf;
      d->d_ino = entry_idx + 1;
      d->d_off = entry_idx + 1;
      d->d_reclen = reclen;
      d->d_type = 0;
      
      int j;
      for (j = 0; j < name_len; j++) {
        d->d_name[j] = data[offset + j];
      }
      d->d_name[name_len] = '\0';

      pl011_puts("  dirent: name=\"");
      pl011_puts(d->d_name);
      pl011_puts("\" reclen=");
      print_hex(reclen);
      pl011_puts("\n");

      if (copy_to_user((uint64_t)user_dirp + written, dirent_buf, reclen) < 0) {
        pl011_puts("krn_getdents64: copy_to_user failed\n");
        return -14; // -EFAULT
      }

      written += reclen;
      current_thread->fds[fd].offset++;
    }

    entry_idx++;
    offset = end + 1;
  }

  pl011_puts("krn_getdents64: success return written=");
  print_hex(written);
  pl011_puts("\n");
  return written;
}

int krn_newfstatat(ARM64Registers* regs, int dfd, const char* user_filename, void* user_statbuf, int flag) {
  char filename[64];
  if (copy_string_from_user(filename, (uint64_t)user_filename, 64) < 0) {
    pl011_puts("krn_newfstatat: copy_string_from_user failed\n");
    return -2; // -ENOENT
  }

  pl011_puts("krn_newfstatat: filename=\"");
  pl011_puts(filename);
  pl011_puts("\" user_statbuf=");
  print_hex((uint64_t)user_statbuf);
  pl011_puts("\" dfd=");
  print_hex(dfd);
  pl011_puts("\n");

  struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t  st_size;
    int32_t  st_blksize;
    int32_t  __pad2;
    int64_t  st_blocks;
    int64_t  st_atime;
    uint64_t st_atime_nsec;
    int64_t  st_mtime;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime;
    uint64_t st_ctime_nsec;
    uint32_t __unused4;
    uint32_t __unused5;
  } st;

  CbMemSet(&st, 0, sizeof(st));

  extern int is_root_dir(const char* path);
  if (is_root_dir(filename)) {
    st.st_mode = 0x41ed; // Directory, 0755
    st.st_nlink = 2;
    st.st_size = 0;
    st.st_blksize = 512;
    st.st_blocks = 0;
  } else {
    struct FsOpenReq req;
    req.sender = current_thread->tid;
    req.cmd = 2; // Open File
    int len = 0;
    while (filename[len] && len < 63) {
      req.filename[len] = filename[len];
      len++;
    }
    req.filename[len] = '\0';

    extern int thread_ipc_send(uint32_t dest, void* buf, uint32_t size);
    extern int thread_ipc_recv(uint32_t src, void* buf, uint32_t size);

    int ret = thread_ipc_send(FS_SERVER_TID, &req, sizeof(req));
    if (ret < 0 && ret != IPC_BLOCKED) {
      pl011_puts("krn_newfstatat: ipc_send failed\n");
      return -5;
    }

    struct FsReply reply;
    ret = thread_ipc_recv(FS_SERVER_TID, &reply, sizeof(reply));
    int n;
    if (ret == IPC_BLOCKED) {
      n = regs->x[0];
    } else {
      n = ret;
    }

    if (n < 4 || reply.size < 0) {
      pl011_puts("krn_newfstatat: file not found on FS server\n");
      return -2; // -ENOENT
    }
    int handle = reply.size;

    struct FsCloseReq size_req;
    size_req.sender = current_thread->tid;
    size_req.cmd = 5; // Get File Size
    size_req.handle = handle;

    ret = thread_ipc_send(FS_SERVER_TID, &size_req, sizeof(size_req));
    if (ret < 0 && ret != IPC_BLOCKED) {
      pl011_puts("krn_newfstatat: size ipc_send failed\n");
      return -5;
    }

    ret = thread_ipc_recv(FS_SERVER_TID, &reply, sizeof(reply));
    if (ret == IPC_BLOCKED) {
      n = regs->x[0];
    } else {
      n = ret;
    }

    int size = (n >= 4) ? reply.size : 0;

    struct FsCloseReq close_req;
    close_req.sender = current_thread->tid;
    close_req.cmd = 4; // Close File
    close_req.handle = handle;
    thread_ipc_send(FS_SERVER_TID, &close_req, sizeof(close_req));
    thread_ipc_recv(FS_SERVER_TID, &reply, sizeof(reply)); // wait for close

    st.st_mode = 0x81ed; // Regular file, 0755
    st.st_nlink = 1;
    st.st_size = size;
    st.st_blksize = 512;
    st.st_blocks = (size + 511) / 512;
  }

  if (copy_to_user((uint64_t)user_statbuf, &st, sizeof(st)) < 0) {
    pl011_puts("krn_newfstatat: copy_to_user failed\n");
    return -14; // -EFAULT
  }

  pl011_puts("krn_newfstatat: success size=");
  print_hex(st.st_size);
  pl011_puts("\n");
  return 0;
}

int krn_fstat(ARM64Registers* regs, int fd, void* user_statbuf) {
  if (fd < 0 || fd >= MAX_KERNEL_FDS) {
    pl011_puts("krn_fstat: EBADF (fd out of range)\n");
    return -9; // -EBADF
  }
  if (!current_thread->fds[fd].used) {
    pl011_puts("krn_fstat: EBADF (fd not used)\n");
    return -9; // -EBADF
  }

  pl011_puts("krn_fstat: fd=");
  print_hex(fd);
  pl011_puts(" user_statbuf=");
  print_hex((uint64_t)user_statbuf);
  pl011_puts("\n");

  struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t  st_size;
    int32_t  st_blksize;
    int32_t  __pad2;
    int64_t  st_blocks;
    int64_t  st_atime;
    uint64_t st_atime_nsec;
    int64_t  st_mtime;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime;
    uint64_t st_ctime_nsec;
    uint32_t __unused4;
    uint32_t __unused5;
  } st;

  CbMemSet(&st, 0, sizeof(st));

  if (current_thread->fds[fd].handle == 999) {
    st.st_mode = 0x41ed; // Directory, 0755
    st.st_nlink = 2;
    st.st_size = 0;
    st.st_blksize = 512;
    st.st_blocks = 0;
  } else {
    struct FsCloseReq size_req;
    size_req.sender = current_thread->tid;
    size_req.cmd = 5; // Get File Size
    size_req.handle = current_thread->fds[fd].handle;

    extern int thread_ipc_send(uint32_t dest, void* buf, uint32_t size);
    extern int thread_ipc_recv(uint32_t src, void* buf, uint32_t size);

    int ret = thread_ipc_send(FS_SERVER_TID, &size_req, sizeof(size_req));
    if (ret < 0 && ret != IPC_BLOCKED) {
      pl011_puts("krn_fstat: size ipc_send failed\n");
      return -5;
    }

    struct FsReply reply;
    ret = thread_ipc_recv(FS_SERVER_TID, &reply, sizeof(reply));
    int n;
    if (ret == IPC_BLOCKED) {
      n = regs->x[0];
    } else {
      n = ret;
    }

    int size = (n >= 4) ? reply.size : 0;

    st.st_mode = 0x81ed; // Regular file, 0755
    st.st_nlink = 1;
    st.st_size = size;
    st.st_blksize = 512;
    st.st_blocks = (size + 511) / 512;
  }

  if (copy_to_user((uint64_t)user_statbuf, &st, sizeof(st)) < 0) {
    pl011_puts("krn_fstat: copy_to_user failed\n");
    return -14; // -EFAULT
  }

  pl011_puts("krn_fstat: success size=");
  print_hex(st.st_size);
  pl011_puts("\n");
  return 0;
}

void syscall_handler(ARM64Registers* regs) {
  uint64_t esr;
  __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
  uint32_t ec = (esr >> 26) & 0x3F;

  if (ec != 0x15) {  // 0x15 is SVC in AArch64
    pl011_puts("\n!!! EL0 SYNC EXCEPTION (not syscall) !!!\n");
    pl011_puts("EC: ");
    print_hex(ec);
    pl011_puts(" ISS: ");
    print_hex(esr & 0x1FFFFFF);
    pl011_puts("\nEL0 PC: ");
    print_hex(regs->pc);
    pl011_puts("\nEL0 LR: ");
    print_hex(regs->x[30]);
    pl011_puts("\nEL0 SP: ");
    print_hex(regs->sp);
    uint64_t tpidr;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tpidr));
    pl011_puts("TPIDR_EL0: ");
    print_hex(tpidr);
    pl011_puts("\nx0="); print_hex(regs->x[0]);
    pl011_puts(" x1="); print_hex(regs->x[1]);
    pl011_puts("\nx2="); print_hex(regs->x[2]);
    pl011_puts(" x3="); print_hex(regs->x[3]);
    pl011_puts("\nx4="); print_hex(regs->x[4]);
    pl011_puts(" x8="); print_hex(regs->x[8]);
    pl011_puts("\nx19="); print_hex(regs->x[19]);
    pl011_puts(" x20="); print_hex(regs->x[20]);
    pl011_puts("\nx21="); print_hex(regs->x[21]);
    pl011_puts(" x22="); print_hex(regs->x[22]);
    pl011_puts("\nx29="); print_hex(regs->x[29]);
    pl011_puts(" x30="); print_hex(regs->x[30]);
    pl011_puts("\n");
    exception_handler_dump(regs->pc, "EL0 Fault");
  }

  thread_set_current_regs(regs);

  uint64_t syscall_num = regs->x[8];
  uint64_t arg0 = regs->x[0];
  uint64_t arg1 = regs->x[1];
  uint64_t arg2 = regs->x[2];
  uint64_t arg3 = regs->x[3];
  uint64_t arg4 = regs->x[4];
  uint64_t arg5 = regs->x[5];

  /*
  pl011_puts("SC ");
  print_hex(syscall_num);
  pl011_puts(" x0=");
  print_hex(arg0);
  pl011_puts(" x1=");
  print_hex(arg1);
  pl011_puts(" PC=");
  print_hex(regs->pc);
  pl011_puts("\n");
  */

  if (syscall_num == 1) {  // SYS_PUTCHAR
    pl011_putc((char)arg0);
    regs->x[0] = 0;               // Success
  } else if (syscall_num == 2) {  // SYS_SEND
    /*
    pl011_puts("KRN: SYS_SEND from ");
    print_hex(thread_get_current_tid());
    pl011_puts(" to ");
    print_hex(arg0);
    pl011_puts("\n");
    */
    int ret = thread_ipc_send((uint32_t)arg0, (void*)arg1, (uint32_t)arg2);
    if (ret != IPC_BLOCKED) {
      regs->x[0] = ret;
    }
  } else if (syscall_num == 3) {  // SYS_RECV
    /*
    pl011_puts("KRN: SYS_RECV from ");
    print_hex(thread_get_current_tid());
    pl011_puts(" src ");
    print_hex(arg0);
    pl011_puts("\n");
    */
    int ret = thread_ipc_recv((uint32_t)arg0, (void*)arg1, (uint32_t)arg2);
    if (ret != IPC_BLOCKED) {
      regs->x[0] = ret;
    }
    /*
    pl011_puts("KRN: SYS_RECV exit, tid=");
    print_hex(thread_get_current_tid());
    pl011_puts(" x0=");
    print_hex(regs->x[0]);
    pl011_puts(" x30=");
    print_hex(regs->x[30]);
    pl011_puts("\n");
    */
  } else if (syscall_num == 4) {  // SYS_GETTID
    regs->x[0] = thread_get_current_tid();
  } else if (syscall_num == 5) {  // SYS_MAP_MMIO
    regs->x[0] = (uint64_t)thread_map_mmio(arg0);
  } else if (syscall_num == 6) {  // SYS_MAP_FB
    regs->x[0] = (uint64_t)thread_map_fb();
  } else if (syscall_num == 7) {  // SYS_SPAWN
    regs->x[0] = thread_create_userspace((const unsigned char*)arg0, (uint32_t)arg1, (const char*)regs->x[2]);
  } else if (syscall_num == 64) { // sys_write
    regs->x[0] = krn_write(regs, arg0, (const void*)arg1, arg2);
  } else if (syscall_num == 63) { // sys_read
    int ret = krn_read(regs, arg0, (void*)arg1, arg2);
    if (ret != IPC_BLOCKED) {
      regs->x[0] = ret;
    }
  } else if (syscall_num == 66) { // sys_writev
    regs->x[0] = krn_writev(regs, arg0, (const struct iovec*)arg1, arg2);
  } else if (syscall_num == 93 || syscall_num == 94) { // sys_exit / sys_exit_group
    pl011_puts("Linux process exited.\n");
    thread_exit();
  } else if (syscall_num == 29) { // sys_ioctl
    // arg0 = fd, arg1 = cmd
    if (arg1 == 0x5401 || arg1 == 0x5413) { // TCGETS / TIOCGWINSZ
      regs->x[0] = (uint64_t)-25; // ENOTTY (to make ash fall back to simple terminal)
    } else {
      regs->x[0] = (uint64_t)-25; // ENOTTY
    }
  } else if (syscall_num == 214) { // sys_brk
    pl011_puts("sys_brk arg0="); print_hex(arg0);
    if (arg0 == 0) {
      regs->x[0] = current_thread->brk;
    } else {
      if (arg0 > current_thread->brk) {
        // Map pages from current_thread->brk to arg0
        uint64_t start = (current_thread->brk + 4095) & ~4095;
        uint64_t end = (arg0 + 4095) & ~4095;
        uint64_t curr;
        uint64_t code_flags = VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_USER;
        for (curr = start; curr < end; curr += 4096) {
          extern uint64_t translate_user_va(Thread* t, uint64_t va);
          if (translate_user_va(current_thread, curr) == 0) {
            void* page = pmm_alloc_page();
            if (!page) {
              pl011_puts(" sys_brk: pmm_alloc_page failed!\n");
              regs->x[0] = current_thread->brk;
              goto brk_done;
            }
            CbMemSet(page, 0, 4096);
            vmm_map(current_thread->pg_dir_phys, curr, (uint64_t)page, code_flags);
          }
        }
      }
      current_thread->brk = arg0;
      regs->x[0] = arg0;
    }
brk_done:
    pl011_puts(" return="); print_hex(regs->x[0]); pl011_puts("\n");
  } else if (syscall_num == 56) { // sys_openat
    regs->x[0] = krn_openat(regs, arg0, (const char*)arg1, arg2);
  } else if (syscall_num == 57) { // sys_close
    regs->x[0] = krn_close(regs, arg0);
  } else if (syscall_num == 61) { // sys_getdents64
    regs->x[0] = krn_getdents64(regs, arg0, (void*)arg1, arg2);
  } else if (syscall_num == 79) { // sys_newfstatat
    regs->x[0] = krn_newfstatat(regs, arg0, (const char*)arg1, (void*)arg2, arg3);
  } else if (syscall_num == 80) { // sys_fstat
    regs->x[0] = krn_fstat(regs, arg0, (void*)arg1);
  } else if (syscall_num == 78 || syscall_num == 96 || syscall_num == 99 || syscall_num == 261 || syscall_num == 278 || syscall_num == 293 || syscall_num == 160) {
    // readlinkat, set_tid_address, set_robust_list, prlimit64, getrandom, rseq, uname
    regs->x[0] = -38; // -ENOSYS
  } else if (syscall_num == 113) { // sys_clock_gettime
    regs->x[0] = -38;
  } else if (syscall_num == 222) { // sys_mmap
    pl011_puts("sys_mmap arg0="); print_hex(arg0);
    pl011_puts(" arg1="); print_hex(arg1);
    pl011_puts(" arg4(fd)="); print_hex(arg4);
    pl011_puts("\n");
    
    extern uint64_t sys_mmap_impl(uint64_t addr, uint64_t size, uint64_t prot);
    regs->x[0] = sys_mmap_impl(arg0, arg1, arg2);
  } else if (syscall_num == 215) { // sys_munmap
    pl011_puts("sys_munmap arg0="); print_hex(arg0); pl011_puts("\n");
    regs->x[0] = 0; // Success
  } else if (syscall_num == 226) { // sys_mprotect
    regs->x[0] = 0;

  } else {
    pl011_puts("Unknown syscall: ");
    print_hex(syscall_num);
    pl011_puts("\n");
    regs->x[0] = -38;  // -ENOSYS
  }
}

void krn_entry(void) {
  pl011_init();
  pl011_puts("\n\n");
  pl011_puts("====================================\n");
  pl011_puts(" OluxOS ARM64 Starting...\n");
  pl011_puts("====================================\n");
  pl011_puts("Booted successfully to EL1.\n");

  // Initialize PMM
  uint64_t mem_start = (uint64_t)_stack_top;
  uint64_t mem_size = 0x48000000ULL - mem_start;
  pmm_init(mem_start, mem_size);

  // Initialize Heap
  heap_init();

  // Initialize VMM
  vmm_init();

  fb_init();

  gicv2_init();
  // gicv2_enable_irq(30);
  // arm_timer_init(100);

#if CONFIG_KDBGER
  kdbger_initialization();
  gicv2_set_irq_target(UART_IRQ, 1);
  gicv2_enable_irq(UART_IRQ);
#endif

  // Initialize threads
  thread_init();

  // Create FOUR userspace threads
  thread_create_userspace(user_shell_bin, user_shell_bin_len, NULL);
  thread_create_userspace(user_shell_bin, user_shell_bin_len, NULL);
  thread_create_userspace(user_shell_bin, user_shell_bin_len, NULL);
  thread_create_userspace(user_shell_bin, user_shell_bin_len, NULL);

  pl011_puts("Starting scheduler...\n");
  while (1) {
    schedule();
  }
}
