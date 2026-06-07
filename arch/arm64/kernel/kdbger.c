#include <arm64/kdbger.h>
#include <arm64/platform.h>
#include <clib.h>
#if CONFIG_BOARD_RPI4
#define UART_BASE 0xFE201000ULL
#else
#define UART_BASE 0x09000000ULL
#endif

#define UART_DR   ((volatile unsigned int*)(UART_BASE + 0x00))
#define UART_FR   ((volatile unsigned int*)(UART_BASE + 0x18))
#define UART_IMSC ((volatile unsigned int*)(UART_BASE + 0x38))
#define UART_ICR  ((volatile unsigned int*)(UART_BASE + 0x44))
#define UART_MIS  ((volatile unsigned int*)(UART_BASE + 0x40))
#define UART_CR   ((volatile unsigned int*)(UART_BASE + 0x30))
#define UART_LCRH ((volatile unsigned int*)(UART_BASE + 0x2C))
#define UART_IFLS ((volatile unsigned int*)(UART_BASE + 0x34))

#define TXFF (1 << 5)
#define RXFE (1 << 4)
#define RXIM (1 << 4)
#define RXIC (1 << 4)

static uint8_t kdbgerState = KDBGER_UNKNOWN;
static int8_t pktBuf[KDBGER_MAXSZ_PKT];
static int32_t idxBuf = 0;

static void kdbgerSetState(kdbgerState_t state) { kdbgerState = state; }
static uint8_t kdbgerGetState(void) { return kdbgerState; }

static void kdbgerIntrEnable(void) {
  *UART_IMSC |= RXIM;
}

static void kdbgerIntrDisable(void) {
  *UART_IMSC &= ~RXIM;
}

static void kdbger_mem_read(uint64_t addr, uint32_t size, uint8_t* out_buf) {
  CbMemCpy(out_buf, (void*)addr, size);
}

static void kdbger_mem_write(uint64_t addr, uint32_t size, const uint8_t* in_buf) {
  CbMemCpy((void*)addr, in_buf, size);
}

extern void pl011_puts(const char* s);
extern void print_hex(uint64_t val);

void kdbger_intr_handler(void) {
  uint32_t mis = *UART_MIS;
  if (!(mis & RXIM)) {
    return;
  }

  kdbgerIntrDisable();
  *UART_ICR = RXIC;

  kdbgerCommPkt_t* pKdbgerCommPkt = (kdbgerCommPkt_t*)pktBuf;
  int8_t* ptr;
  uint32_t sz;
  uint64_t addr;

  if (kdbgerGetState() == KDBGER_UNKNOWN) {
    pl011_puts("KDBGER: unknown state\n");
    goto done;
  }

  while (!(*UART_FR & RXFE)) {
    if (kdbgerGetState() == KDBGER_READY) {
      kdbgerSetState(KDBGER_PKT_RECV);
      idxBuf = 0;
      CbMemSet(pktBuf, 0, KDBGER_MAXSZ_PKT);
    }

    if (idxBuf < KDBGER_MAXSZ_PKT) {
      pktBuf[idxBuf++] = (char)(*UART_DR & 0xFF);
    }

    if ((kdbgerGetState() == KDBGER_PKT_RECV) &&
        (idxBuf >= sizeof(kdbgerCommHdr_t)) &&
        (idxBuf >= pKdbgerCommPkt->kdbgerCommHdr.pktLen)) {
      
      kdbgerSetState(KDBGER_PKT_DONE);
      break;
    }
  }

  if (kdbgerGetState() == KDBGER_PKT_DONE) {
    switch (pKdbgerCommPkt->kdbgerCommHdr.opCode) {
      case KDBGER_REQ_CONNECT:
        pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_CONNECT;
        pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof(kdbgerCommHdr_t);
        pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
        break;

      case KDBGER_REQ_MEM_READ:
        ptr = (int8_t*)&pKdbgerCommPkt->kdbgerRspMemReadPkt.memContent;
        addr = pKdbgerCommPkt->kdbgerReqMemReadPkt.address;
        sz = pKdbgerCommPkt->kdbgerReqMemReadPkt.size;

        kdbger_mem_read(addr, sz, (uint8_t*)ptr);

        pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_MEM_READ;
        pKdbgerCommPkt->kdbgerCommHdr.pktLen =
            sizeof(kdbgerRspMemReadPkt_t) - sizeof(int8_t*) + sz;
        pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
        pKdbgerCommPkt->kdbgerRspMemReadPkt.address = addr;
        pKdbgerCommPkt->kdbgerRspMemReadPkt.size = sz;
        break;

      case KDBGER_REQ_MEM_WRITE:
        ptr = (int8_t*)&pKdbgerCommPkt->kdbgerReqMemWritePkt.memContent;
        addr = pKdbgerCommPkt->kdbgerReqMemWritePkt.address;
        sz = pKdbgerCommPkt->kdbgerReqMemWritePkt.size;

        kdbger_mem_write(addr, sz, (const uint8_t*)ptr);

        pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_MEM_WRITE;
        pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof(kdbgerRspMemWritePkt_t);
        pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
        pKdbgerCommPkt->kdbgerRspMemWritePkt.address = addr;
        pKdbgerCommPkt->kdbgerRspMemWritePkt.size = sz;
        break;

      case KDBGER_REQ_PCI_LIST:
        pKdbgerCommPkt->kdbgerRspPciListPkt.numOfPciDevice = 0;
        pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_PCI_LIST;
        pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof(kdbgerRspPciListPkt_t) - sizeof(kdbgerPciDev_t*);
        pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
        break;

      case KDBGER_REQ_E810_LIST:
        pKdbgerCommPkt->kdbgerRspE820ListPkt.numOfE820Record = 1;
        pKdbgerCommPkt->kdbgerRspE820ListPkt.e820ListContent[0].baseAddr = 0x40000000ULL;
        pKdbgerCommPkt->kdbgerRspE820ListPkt.e820ListContent[0].length = 0x80000000ULL;
        pKdbgerCommPkt->kdbgerRspE820ListPkt.e820ListContent[0].type = 1;
        pKdbgerCommPkt->kdbgerRspE820ListPkt.e820ListContent[0].attr = 1;

        pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_E810_LIST;
        pKdbgerCommPkt->kdbgerCommHdr.pktLen =
            sizeof(kdbgerRspE820ListPkt_t) - sizeof(kdbgerE820record_t*) +
            sizeof(kdbgerE820record_t);
        pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
        break;

      default:
        pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_NACK;
        pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof(kdbgerCommHdr_t);
        pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
        break;
    }

    kdbgerSetState(KDBGER_PKT_TRAN);

    int32_t i;
    for (i = 0; i < pKdbgerCommPkt->kdbgerCommHdr.pktLen; i++) {
      while (*UART_FR & TXFF);
      *UART_DR = pktBuf[i];
    }

    kdbgerSetState(KDBGER_READY);
  }

done:
  kdbgerIntrEnable();
}

void kdbger_initialization(void) {
  kdbgerSetState(KDBGER_INIT);
  kdbgerIntrDisable();

  // 1. Disable UART
  *UART_CR = 0;

  // 2. Clear pending interrupts
  *UART_ICR = 0x7FF;

  // 3. Set RX interrupt FIFO trigger level to 1/8 (lowest possible)
  *UART_IFLS = 0; 

  // 4. Enable FIFOs and set 8N1
  *UART_LCRH = (3 << 5) | (1 << 4);

  // 5. Enable UART, TX, RX
  *UART_CR = 1 | (1 << 8) | (1 << 9);

  kdbgerSetState(KDBGER_READY);
  kdbgerIntrEnable();
}
