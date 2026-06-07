#ifndef __KDBGER_PKT_H__
#define __KDBGER_PKT_H__

#include <types.h>

#define KDBGER_MAXSZ_PKT 2048
#define KDBGER_FIFO_SZ 14
#define KDBGER_SECTOR_SZ 512
#define KDBGER_SECTOR_SZULL 512ULL
#define KDBGER_IDE_BUF (KDBGER_SECTOR_SZ * 2)
#define KDBGER_CMOS_ADDR 0x70
#define KDBGER_CMOS_DATA 0x71

typedef enum {

  KDBGER_UNKNOWN = 0,
  KDBGER_INIT,
  KDBGER_READY,
  KDBGER_PKT_TRAN,
  KDBGER_PKT_RECV,
  KDBGER_PKT_DONE,

} kdbgerState_t;

typedef enum {

  KDBGER_REQ_CONNECT = 1,
  KDBGER_RSP_CONNECT,

  KDBGER_REQ_MEM_READ,
  KDBGER_RSP_MEM_READ,

  KDBGER_REQ_MEM_WRITE,
  KDBGER_RSP_MEM_WRITE,

  KDBGER_REQ_IO_READ,
  KDBGER_RSP_IO_READ,

  KDBGER_REQ_IO_WRITE,
  KDBGER_RSP_IO_WRITE,

  KDBGER_REQ_PCI_READ,
  KDBGER_RSP_PCI_READ,

  KDBGER_REQ_PCI_WRITE,
  KDBGER_RSP_PCI_WRITE,

  KDBGER_REQ_IDE_READ,
  KDBGER_RSP_IDE_READ,

  KDBGER_REQ_IDE_WRITE,
  KDBGER_RSP_IDE_WRITE,

  KDBGER_REQ_CMOS_READ,
  KDBGER_RSP_CMOS_READ,

  KDBGER_REQ_CMOS_WRITE,
  KDBGER_RSP_CMOS_WRITE,

  KDBGER_REQ_PCI_LIST,
  KDBGER_RSP_PCI_LIST,

  KDBGER_REQ_E810_LIST,
  KDBGER_RSP_E810_LIST,

  KDBGER_RSP_CPU_EXCEPTION,

  KDBGER_RSP_NACK,

} kdbgerOpCode_t;

typedef enum _kdbgErrorCode {

  KDBGER_SUCCESS = 0,
  KDBGER_FAILURE,

} kdbgErrorCode_t;

typedef struct PACKED {
  uint16_t bus;
  uint8_t dev;
  uint8_t fun;
  uint16_t vendorId;
  uint16_t deviceId;

} kdbgerPciDev_t;

typedef struct PACKED {
  uint64_t baseAddr;
  uint64_t length;
  uint32_t type;
  uint32_t attr;

} kdbgerE820record_t;

// Common packet
typedef struct PACKED _kdbgerCommHdr {
  uint16_t opCode;

  union {
    uint16_t pad;
    uint16_t errorCode;
  };

  uint32_t pktLen;

} kdbgerCommHdr_t;

// Memory space Read/Write packets
typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint64_t address;
  uint32_t size;

} kdbgerReqMemReadPkt_t, kdbgerRspMemWritePkt_t;

typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint64_t address;
  uint32_t size;
  uint8_t* memContent;

} kdbgerRspMemReadPkt_t, kdbgerReqMemWritePkt_t;

// IO space Read/Write packets
typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint16_t address;
  uint32_t size;

} kdbgerReqIoReadPkt_t, kdbgerRspIoWritePkt_t;

typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint16_t address;
  uint32_t size;
  uint8_t* ioContent;

} kdbgerRspIoReadPkt_t, kdbgerReqIoWritePkt_t;

// PCI config Read/Write packets
typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint32_t address;
  uint16_t size;

} kdbgerReqPciReadPkt_t, kdbgerRspPciWritePkt_t;

typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint32_t address;
  uint16_t size;
  uint8_t* pciContent;

} kdbgerRspPciReadPkt_t, kdbgerReqPciWritePkt_t;

// IDE Read/Write packets
typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint64_t address;
  uint32_t size;

} kdbgerReqIdeReadPkt_t, kdbgerRspIdeWritePkt_t;

typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint64_t address;
  uint32_t size;
  uint8_t* ideContent;

} kdbgerRspIdeReadPkt_t, kdbgerReqIdeWritePkt_t;

// CMOS Read/Write packets
typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint8_t address;
  uint8_t size;

} kdbgerReqCmosReadPkt_t, kdbgerRspCmosWritePkt_t;

typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint8_t address;
  uint8_t size;
  uint8_t* cmosContent;

} kdbgerRspCmosReadPkt_t, kdbgerReqCmosWritePkt_t;

typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;

} kdbgerReqPciListPkt_t, kdbgerReqE820ListPkt_t;

typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint32_t numOfPciDevice;
  kdbgerPciDev_t* pciListContent;

} kdbgerRspPciListPkt_t;

typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint32_t numOfE820Record;
  kdbgerE820record_t e820ListContent[1];

} kdbgerRspE820ListPkt_t;

typedef struct PACKED {
  kdbgerCommHdr_t kdbgerCommHdr;
  uint32_t exNum;

} kdbgerRspCpuExceptionPkt_t;

typedef struct PACKED {
  union {
    // Common
    kdbgerCommHdr_t kdbgerCommHdr;

    // Memory Read
    kdbgerReqMemReadPkt_t kdbgerReqMemReadPkt;
    kdbgerRspMemReadPkt_t kdbgerRspMemReadPkt;

    // Memory Write
    kdbgerReqMemWritePkt_t kdbgerReqMemWritePkt;
    kdbgerRspMemWritePkt_t kdbgerRspMemWritePkt;

    // IO Read
    kdbgerReqIoReadPkt_t kdbgerReqIoReadPkt;
    kdbgerRspIoReadPkt_t kdbgerRspIoReadPkt;

    // IO Write
    kdbgerReqIoWritePkt_t kdbgerReqIoWritePkt;
    kdbgerRspIoWritePkt_t kdbgerRspIoWritePkt;

    // PCI Read
    kdbgerReqPciReadPkt_t kdbgerReqPciReadPkt;
    kdbgerRspPciReadPkt_t kdbgerRspPciReadPkt;

    // PCI Write
    kdbgerReqPciWritePkt_t kdbgerReqPciWritePkt;
    kdbgerRspPciWritePkt_t kdbgerRspPciWritePkt;

    // IDE Read
    kdbgerReqIdeReadPkt_t kdbgerReqIdeReadPkt;
    kdbgerRspIdeReadPkt_t kdbgerRspIdeReadPkt;

    // IDE Write
    kdbgerReqIdeWritePkt_t kdbgerReqIdeWritePkt;
    kdbgerRspIdeWritePkt_t kdbgerRspIdeWritePkt;

    // CMOS Read
    kdbgerReqCmosReadPkt_t kdbgerReqCmosReadPkt;
    kdbgerRspCmosReadPkt_t kdbgerRspCmosReadPkt;

    // CMOS Write
    kdbgerReqCmosWritePkt_t kdbgerReqCmosWritePkt;
    kdbgerRspCmosWritePkt_t kdbgerRspCmosWritePkt;

    // PCI List
    kdbgerReqPciListPkt_t kdbgerReqPciListPkt;
    kdbgerRspPciListPkt_t kdbgerRspPciListPkt;

    // E820 List
    kdbgerReqE820ListPkt_t kdbgerReqE820ListPkt;
    kdbgerRspE820ListPkt_t kdbgerRspE820ListPkt;

    // CPU Exception
    kdbgerRspCpuExceptionPkt_t kdbgerRspCpuExceptionPkt;
  };

} kdbgerCommPkt_t;

#endif // __KDBGER_PKT_H__
