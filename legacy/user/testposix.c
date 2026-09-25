#include <syscall.h>

void puts(const char* s) {
  int len = 0;
  while (s[len]) len++;
  sys_write(1, s, len);
}

int main(int argc, char* argv[]) {
  puts("Test POSIX Syscalls starting...\n");

  // Open /busybox
  int fd = sys_openat(-100, "/busybox", 0);
  if (fd < 0) {
    puts("FAIL: sys_openat failed!\n");
    sys_exit(1);
  }
  puts("SUCCESS: sys_openat returned fd: ");
  // Print fd (simple conversion for single digit)
  char fd_char = '0' + fd;
  sys_write(1, &fd_char, 1);
  puts("\n");

  // Read 100 bytes
  char buf[100];
  int n = sys_read(fd, buf, 100);
  if (n < 0) {
    puts("FAIL: sys_read failed!\n");
    sys_close(fd);
    sys_exit(1);
  }
  puts("SUCCESS: sys_read read bytes: ");
  // Print bytes read
  // (Assuming it read exactly 100 bytes, we print '1' '0' '0')
  if (n == 100) {
    puts("100\n");
  } else {
    puts("not 100\n");
  }

  // Write first 20 bytes to stdout (should be ELF header!)
  puts("First 20 bytes of /busybox (hex/char? let's print as char for magic):\n");
  // ELF magic is 7f 45 4c 46 (ELFs)
  // We can check if it matches
  if (buf[0] == 0x7f && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
    puts("ELF magic detected!\n");
  } else {
    puts("ELF magic NOT detected!\n");
  }

  // Close file
  int ret = sys_close(fd);
  if (ret < 0) {
    puts("FAIL: sys_close failed!\n");
    sys_exit(1);
  }
  puts("SUCCESS: sys_close completed.\n");

  puts("Test POSIX Syscalls completed successfully!\n");
  sys_exit(0);
  return 0;
}
