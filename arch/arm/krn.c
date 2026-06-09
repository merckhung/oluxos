#include <types.h>

// Forward declarations for PL011 driver functions
void pl011_putc(char c);
void pl011_puts(const char* s);
char pl011_getc(void);

// Simple strcmp function
int strcmp(const char* s1, const char* s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void krn_entry(void) {
  pl011_puts("\n\n");
  pl011_puts("====================================\n");
  pl011_puts(" OluxOS ARM32 Starting...\n");
  pl011_puts("====================================\n");
  pl011_puts("Booted successfully to Cortex-A15 (ARM32).\n\n");

  char cmd[100];
  int idx = 0;

  pl011_puts("OluxOS ARM32 > ");

  while (1) {
    char c = pl011_getc();

    if (c == '\r' || c == '\n') {
      pl011_putc('\r');
      pl011_putc('\n');
      cmd[idx] = '\0';

      if (strcmp(cmd, "help") == 0) {
        pl011_puts("Supported commands: help, hello, reboot\n");
      } else if (strcmp(cmd, "hello") == 0) {
        pl011_puts("Hello from ARM32 OluxOS!\n");
      } else if (strcmp(cmd, "reboot") == 0) {
        pl011_puts("Rebooting...\n");
        // PSCI System Reset (0x84000009)
        __asm__ __volatile__(
            ".arch_extension virt\n"
            "mov r0, #0x84000000\n"
            "orr r0, r0, #0x9\n" // PSCI_0_2_FN_SYSTEM_RESET
            "hvc #0\n"           // Hypervisor call
        );
        // Fallback: spin
        while(1);
      } else if (idx > 0) {
        pl011_puts("Unknown command: ");
        pl011_puts(cmd);
        pl011_puts("\n");
      }
      pl011_puts("OluxOS ARM32 > ");
      idx = 0;
    } else if (c == 127 || c == 8) { // Backspace
      if (idx > 0) {
        idx--;
        pl011_putc('\b');
        pl011_putc(' ');
        pl011_putc('\b');
      }
    } else if (idx < 99) {
      cmd[idx++] = c;
      pl011_putc(c);
    }
  }
}
