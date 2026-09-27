#ifndef __ARM64_KDBGER_H__
#define __ARM64_KDBGER_H__

#include <kdbger_pkt.h>

typedef enum {
  UART_PORT0 = 0,
} kdbgerDebugPort_t;

void kdbger_initialization(void);
void kdbger_intr_handler(void);

#endif  // __ARM64_KDBGER_H__
