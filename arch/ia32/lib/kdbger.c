/*
 * Copyright (C) 2006 -  2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: kdbger.c
 * Description:
 * 	OluxOS IA32 Kernel Debugger
 *
 */
#include <clib.h>
#include <driver/ide.h>
#include <driver/pci.h>
#include <ia32/bios.h>
#include <ia32/debug.h>
#include <ia32/gdb.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/kdbger.h>
#include <ia32/page.h>
#include <ia32/platform.h>
#include <types.h>

ExternIRQHandler(4);

static uint16_t kdbgerPortAddr = UART_PORT0;
static uint8_t kdbgerState = KDBGER_UNKNOWN;
static int8_t pktBuf[KDBGER_MAXSZ_PKT];
static int32_t idxBuf;

static void kdbgerSetState(kdbgerState_t state) { kdbgerState = state; }

static uint8_t kdbgerGetState(void) { return kdbgerState; }

static void kdbgerIntrEnable(void) {
  IoOutByte(UART_IER_RXRD, kdbgerPortAddr + UART_REG_IER);
}

static void kdbgerIntrDisable(void) {
  IoOutByte(0x00, kdbgerPortAddr + UART_REG_IER);
}

void kdbgerIDEReadSector(uint32_t sector, uint8_t* buf) {
  int32_t i, j;
  uint16_t tmp;

  IoOutByte(0x01, IDE_NSECTOR);
  IoOutByte((sector & 0x000000FF), IDE_SECTOR);
  IoOutByte((sector & 0x0000FF00) >> 8, IDE_LOWCYL);
  IoOutByte((sector & 0x00FF0000) >> 16, IDE_HIGHCYL);
  IoOutByte(((sector & 0x0F000000) >> 24) | 0xE0, IDE_SELECT);
  IoOutByte(0x20, IDE_COMMAND);

  for (i = 0, j = 0; i < (KDBGER_SECTOR_SZ / 2); i++, j += 2) {
    tmp = IoInWord(IDE_DATA);
    buf[j] = tmp & 0x00FF;
    buf[j + 1] = (tmp >> 8) & 0x00FF;
  }
}

void kdbgerIDEWriteSector(uint32_t sector, uint8_t* buf) {
  int32_t i, j;

  IoOutByte(0x01, IDE_NSECTOR);
  IoOutByte((sector & 0x000000FF), IDE_SECTOR);
  IoOutByte((sector & 0x0000FF00) >> 8, IDE_LOWCYL);
  IoOutByte((sector & 0x00FF0000) >> 16, IDE_HIGHCYL);
  IoOutByte(((sector & 0x0F000000) >> 24) | 0xE0, IDE_SELECT);
  IoOutByte(0x30, IDE_COMMAND);

  for (i = 0, j = 0; i < (KDBGER_SECTOR_SZ / 2); i++, j += 2) {
    IoOutWord(*(buf + j) | (*(buf + j + 1) << 8), IDE_DATA);
  }
}

static int32_t kdbgerIdeReadWrite(uint64_t addr, uint32_t sz, uint8_t* ptr, kdbgerOpCode_t op) {
  uint8_t ideBuf[KDBGER_SECTOR_SZ];
  uint32_t ideSector, ideNrSector, ideOffset;
  uint32_t i;

  if (op != KDBGER_REQ_IDE_READ && op != KDBGER_REQ_IDE_WRITE) return -1;

  ideSector = addr / KDBGER_SECTOR_SZULL;
  ideOffset = addr % KDBGER_SECTOR_SZULL;
  ideNrSector = sz / KDBGER_SECTOR_SZ;

  // Unalign
  if (ideOffset || !ideNrSector) ideNrSector++;

  switch (op) {
    case KDBGER_REQ_IDE_READ:

      // Read IDE device
      for (i = 0; i < ideNrSector; i++) {
        // Read a sector into buffer
        kdbgerIDEReadSector(ideSector + i, ideBuf);
        if (!i && ideOffset) {
          // First
          if (sz < KDBGER_SECTOR_SZ) {
            CbMemCpy(ptr, ideBuf + ideOffset, sz);
            ptr += sz;
          } else {
            CbMemCpy(ptr, ideBuf + ideOffset, KDBGER_SECTOR_SZ - ideOffset);
            ptr += (KDBGER_SECTOR_SZ - ideOffset);
          }
        } else if (i == (ideNrSector - 1) && ideOffset) {
          // Last
          CbMemCpy(ptr, ideBuf, ideOffset);
          ptr += ideOffset;
        } else {
          // Normal
          if (sz < KDBGER_SECTOR_SZ) {
            CbMemCpy(ptr, ideBuf, sz);
            ptr += sz;
          } else {
            CbMemCpy(ptr, ideBuf, KDBGER_SECTOR_SZ);
            ptr += KDBGER_SECTOR_SZ;
          }
        }
      }
      break;

    case KDBGER_REQ_IDE_WRITE:

      // Write IDE device
      for (i = 0; i < ideNrSector; i++) {
        if (!i && ideOffset) {
          // First
          kdbgerIDEReadSector(ideSector + i, ideBuf);
          if (sz < KDBGER_SECTOR_SZ) {
            CbMemCpy(ideBuf + ideOffset, ptr, sz);
            ptr += sz;
          } else {
            CbMemCpy(ideBuf + ideOffset, ptr, KDBGER_SECTOR_SZ - ideOffset);
            ptr += (KDBGER_SECTOR_SZ - ideOffset);
          }
        } else if (i == (ideNrSector - 1) && ideOffset) {
          // Last
          kdbgerIDEReadSector(ideSector + i, ideBuf);
          CbMemCpy(ideBuf, ptr, ideOffset);
          ptr += ideOffset;
        } else {
          // Normal
          kdbgerIDEReadSector(ideSector + i, ideBuf);
          if (sz < KDBGER_SECTOR_SZ) {
            CbMemCpy(ideBuf, ptr, sz);
            ptr += sz;
          } else {
            CbMemCpy(ideBuf, ptr, KDBGER_SECTOR_SZ);
            ptr += KDBGER_SECTOR_SZ;
          }
        }

        // Read a sector into buffer
        kdbgerIDEWriteSector(ideSector + i, ideBuf);
      }
      break;

    default:
      return -1;
  }

  return 0;
}

static uint32_t kdbgerPciDetectDevice(kdbgerPciDev_t* pKdbgerPciDev) {
  uint32_t value;
  uint16_t bus;
  uint8_t dev, func;
  uint32_t count = 0;

  for (bus = 0; bus <= PCI_BUS_MAX; bus++)
    for (dev = 0; dev <= PCI_DEV_MAX; dev++)
      for (func = 0; func <= PCI_FUN_MAX; func++) {
        value = PciReadConfigDWord(PciCalBaseAddr(bus, dev, func), 0);
        if (value != 0xFFFFFFFF) {
          pKdbgerPciDev->bus = bus;
          pKdbgerPciDev->dev = dev;
          pKdbgerPciDev->fun = func;
          pKdbgerPciDev->vendorId = value & 0xFFFF;
          pKdbgerPciDev->deviceId = (value >> 16) & 0xFFFF;

          count++;
          pKdbgerPciDev++;
        }
      }

  return count;
}

static uint32_t kdbgerE820Copy(kdbgerE820record_t* pKdbgerE820record) {
  int32_t i;
  uint32_t count = 0;
  volatile uint8_t* e820_count = (volatile uint8_t*)E820_COUNT;
  volatile E820Result* e820_base = (volatile E820Result*)E820_BASE;

  for (i = 0; i < *e820_count; i++, pKdbgerE820record++, count++) {
    pKdbgerE820record->baseAddr =
        (((uint64_t)(e820_base + i)->BaseAddrHigh) & 0xFFFFFFFFULL) << 32;
    pKdbgerE820record->baseAddr |=
        ((uint64_t)(e820_base + i)->BaseAddrLow) & 0xFFFFFFFFULL;
    pKdbgerE820record->length =
        (((uint64_t)(e820_base + i)->LengthHigh) & 0xFFFFFFFFULL) << 32;
    pKdbgerE820record->length |=
        ((uint64_t)(e820_base + i)->LengthLow) & 0xFFFFFFFFULL;
    pKdbgerE820record->type = (e820_base + i)->RecType;
    pKdbgerE820record->attr = (e820_base + i)->Attributes;
  }

  return count;
}

static void kdbgerFifoEnable(void) {
  // FIFO Enable, TX & RX, 14 bytes
  IoOutByte((UART_FCR_FE | UART_FCR_RFR | UART_FCR_XFR | UART_FCR_TL_14B),
            kdbgerPortAddr + UART_REG_FCR);
}

static void kdbgerIntHandler(uint8_t IrqNum) {
  uint8_t lsrState;
  kdbgerCommPkt_t* pKdbgerCommPkt = (kdbgerCommPkt_t*)pktBuf;
  int8_t* ptr;
  volatile uint8_t* phyMem;
  uint32_t i, sz;
  uint64_t addr;
  uint16_t ioAddr;
  uint32_t pciAddr;
  uint16_t pciSz;
  int8_t restBuf[KDBGER_FIFO_SZ];
  int32_t restLen;
  uint8_t cmosAddr, cmosSz;

  // Disable interrupt
  kdbgerIntrDisable();

  // Make sure the state != UNKNOWN
  if (kdbgerGetState() == KDBGER_UNKNOWN) {
    DbgPrint("Kernel Debugger internal state is unknown\n");
    goto done;
  }

  // Read LSR
  lsrState = IoInByte(kdbgerPortAddr + UART_REG_LSR);
  if (lsrState & UART_LSR_RXDR) {
    // Receive & assemble a request packet
    if (kdbgerGetState() == KDBGER_READY) {
      // Transit to RECV state
      kdbgerSetState(KDBGER_PKT_RECV);
      idxBuf = 0;
      CbMemSet(pktBuf, 0, KDBGER_MAXSZ_PKT);
    } else if (kdbgerGetState() != KDBGER_PKT_RECV) {
      // Internal error
      kdbgerSetState(KDBGER_UNKNOWN);
      goto done;
    }

    // Receive data of a request packet from FIFO
    for (;;) {
      // Read a byte
      pktBuf[idxBuf++] = IoInByte(kdbgerPortAddr + UART_REG_RBR);
      if (!(IoInByte(kdbgerPortAddr + UART_REG_LSR) & UART_LSR_RXDR)) break;
    }

    // Response to a request
    if ((kdbgerGetState() == KDBGER_PKT_RECV) &&
        (idxBuf >= sizeof(kdbgerCommHdr_t)) &&
        (idxBuf >= pKdbgerCommPkt->kdbgerCommHdr.pktLen)) {
      if (idxBuf != pKdbgerCommPkt->kdbgerCommHdr.pktLen) {
        restLen = idxBuf - pKdbgerCommPkt->kdbgerCommHdr.pktLen;
        if (restLen <= KDBGER_FIFO_SZ)
          CbMemCpy(restBuf, pktBuf + pKdbgerCommPkt->kdbgerCommHdr.pktLen,
                   restLen);
        else {
          // Internal error
          kdbgerSetState(KDBGER_UNKNOWN);
          goto done;
        }
      } else
        restLen = 0;

      // Indicate receiving has done
      kdbgerSetState(KDBGER_PKT_DONE);

      // Look at OpCode
      switch (pKdbgerCommPkt->kdbgerCommHdr.opCode) {
        case KDBGER_REQ_CONNECT:

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_CONNECT;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof(kdbgerCommHdr_t);
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          break;

        case KDBGER_REQ_MEM_READ:

          // Read memory content
          ptr = (int8_t*)&pKdbgerCommPkt->kdbgerRspMemReadPkt.memContent;
          addr = pKdbgerCommPkt->kdbgerReqMemReadPkt.address;
          phyMem = (volatile uint8_t*)(uint32_t)addr;
          sz = pKdbgerCommPkt->kdbgerReqMemReadPkt.size;

          for (i = 0; i < sz; i++) {
            *(ptr + i) = *(phyMem + i);
          }

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_MEM_READ;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen =
              sizeof(kdbgerRspMemReadPkt_t) - sizeof(int8_t*) + sz;
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          pKdbgerCommPkt->kdbgerRspMemReadPkt.address = addr;
          pKdbgerCommPkt->kdbgerRspMemReadPkt.size = sz;
          break;

        case KDBGER_REQ_MEM_WRITE:

          // write memory content
          ptr = (int8_t*)&pKdbgerCommPkt->kdbgerReqMemWritePkt.memContent;
          addr = pKdbgerCommPkt->kdbgerReqMemWritePkt.address;
          phyMem = (volatile uint8_t*)(uint32_t)addr;
          sz = pKdbgerCommPkt->kdbgerReqMemWritePkt.size;

          for (i = 0; i < sz; i++) {
            *(phyMem + i) = *(ptr + i);
          }

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_MEM_WRITE;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof(kdbgerRspMemWritePkt_t);
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          pKdbgerCommPkt->kdbgerRspMemWritePkt.address = addr;
          pKdbgerCommPkt->kdbgerRspMemWritePkt.size = sz;
          break;

        case KDBGER_REQ_IO_READ:

          // Read IO content
          ptr = (int8_t*)&pKdbgerCommPkt->kdbgerRspIoReadPkt.ioContent;
          ioAddr = pKdbgerCommPkt->kdbgerReqIoReadPkt.address;
          sz = pKdbgerCommPkt->kdbgerReqIoReadPkt.size;

          for (i = 0; i < sz; i++) {
            // Read IO data
            *(ptr + i) = IoInByte(ioAddr + i);
          }

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_IO_READ;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen =
              sizeof(kdbgerRspIoReadPkt_t) - sizeof(int8_t*) + sz;
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          pKdbgerCommPkt->kdbgerRspIoReadPkt.address = ioAddr;
          pKdbgerCommPkt->kdbgerRspIoReadPkt.size = sz;
          break;

        case KDBGER_REQ_IO_WRITE:

          // Write IO content
          ptr = (int8_t*)&pKdbgerCommPkt->kdbgerReqIoWritePkt.ioContent;
          ioAddr = pKdbgerCommPkt->kdbgerReqIoReadPkt.address;
          sz = pKdbgerCommPkt->kdbgerReqIoReadPkt.size;

          for (i = 0; i < sz; i++) {
            // Write IO data
            IoOutByte(*(ptr + i), ioAddr + i);
          }

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_IO_WRITE;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof(kdbgerRspIoWritePkt_t);
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          pKdbgerCommPkt->kdbgerRspIoWritePkt.address = ioAddr;
          pKdbgerCommPkt->kdbgerRspIoWritePkt.size = sz;
          break;

        case KDBGER_REQ_PCI_READ:

          // Read PCI config space
          ptr = (int8_t*)&pKdbgerCommPkt->kdbgerRspPciReadPkt.pciContent;
          pciAddr = pKdbgerCommPkt->kdbgerReqPciReadPkt.address;
          pciSz = pKdbgerCommPkt->kdbgerReqPciReadPkt.size;
          for (i = 0; i < pciSz; i++) {
            // Read PCI config data
            *(ptr + i) = PciReadConfigByte(pciAddr, i);
          }

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_PCI_READ;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen =
              sizeof(kdbgerRspPciReadPkt_t) - sizeof(int8_t*) + pciSz;
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          pKdbgerCommPkt->kdbgerRspPciReadPkt.address = pciAddr;
          pKdbgerCommPkt->kdbgerRspPciReadPkt.size = pciSz;
          break;

        case KDBGER_REQ_PCI_WRITE:

          // Write PCI config space
          ptr = (int8_t*)&pKdbgerCommPkt->kdbgerReqPciWritePkt.pciContent;
          pciAddr = pKdbgerCommPkt->kdbgerReqPciReadPkt.address;
          pciSz = pKdbgerCommPkt->kdbgerReqPciReadPkt.size;

          for (i = 0; i < pciSz; i++) {
            // Write PCI config data
            PciWriteConfigByte(pciAddr, i, *(ptr + i));
          }

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_PCI_WRITE;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof(kdbgerRspPciWritePkt_t);
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          pKdbgerCommPkt->kdbgerRspPciWritePkt.address = pciAddr;
          pKdbgerCommPkt->kdbgerRspPciWritePkt.size = pciSz;
          break;

        case KDBGER_REQ_IDE_READ:

          // Read IDE device
          ptr = (int8_t*)&pKdbgerCommPkt->kdbgerRspIdeReadPkt.ideContent;
          addr = pKdbgerCommPkt->kdbgerReqIdeReadPkt.address;
          sz = pKdbgerCommPkt->kdbgerReqIdeReadPkt.size;

          // Read IDE data
          kdbgerIdeReadWrite(addr, sz, (uint8_t*)ptr, KDBGER_REQ_IDE_READ);

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_IDE_READ;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen =
              sizeof(kdbgerRspIdeReadPkt_t) - sizeof(int8_t*) + sz;
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          pKdbgerCommPkt->kdbgerRspIdeReadPkt.address = addr;
          pKdbgerCommPkt->kdbgerRspIdeReadPkt.size = sz;
          break;

        case KDBGER_REQ_IDE_WRITE:

          // Write IDE device
          ptr = (int8_t*)&pKdbgerCommPkt->kdbgerReqIdeWritePkt.ideContent;
          addr = pKdbgerCommPkt->kdbgerReqIdeReadPkt.address;
          sz = pKdbgerCommPkt->kdbgerReqIdeReadPkt.size;

          // Write IDE data
          kdbgerIdeReadWrite(addr, sz, (uint8_t*)ptr, KDBGER_REQ_IDE_WRITE);

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_IDE_WRITE;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof(kdbgerRspIdeWritePkt_t);
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          pKdbgerCommPkt->kdbgerRspIdeWritePkt.address = addr;
          pKdbgerCommPkt->kdbgerRspIdeWritePkt.size = sz;
          break;

        case KDBGER_REQ_CMOS_READ:

          // Read CMOS device
          ptr = (int8_t*)&pKdbgerCommPkt->kdbgerRspCmosReadPkt.cmosContent;
          cmosAddr = pKdbgerCommPkt->kdbgerReqCmosReadPkt.address;
          cmosSz = pKdbgerCommPkt->kdbgerReqCmosReadPkt.size;

          // Read CMOS data
          for (i = 0; i < cmosSz; i++) {
            // Write CMOS address
            IoOutByte(i, KDBGER_CMOS_ADDR);
            // Read CMOS data
            *(ptr + i) = IoInByte(KDBGER_CMOS_DATA);
          }

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_CMOS_READ;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen =
              sizeof(kdbgerRspCmosReadPkt_t) - sizeof(int8_t*) + cmosSz;
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          pKdbgerCommPkt->kdbgerRspCmosReadPkt.address = cmosAddr;
          pKdbgerCommPkt->kdbgerRspCmosReadPkt.size = cmosSz;
          break;

        case KDBGER_REQ_CMOS_WRITE:

          // Write CMOS device
          ptr = (int8_t*)&pKdbgerCommPkt->kdbgerReqCmosWritePkt.cmosContent;
          cmosAddr = pKdbgerCommPkt->kdbgerReqCmosReadPkt.address;
          cmosSz = pKdbgerCommPkt->kdbgerReqCmosReadPkt.size;

          // Write CMOS data
          for (i = 0; i < cmosSz; i++) {
            // Write CMOS address
            IoOutByte(i, KDBGER_CMOS_ADDR);
            // Write CMOS data
            IoOutByte(*(ptr + i), KDBGER_CMOS_DATA);
          }

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_CMOS_WRITE;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen =
              sizeof(kdbgerRspCmosWritePkt_t);
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          pKdbgerCommPkt->kdbgerRspCmosWritePkt.address = cmosAddr;
          pKdbgerCommPkt->kdbgerRspCmosWritePkt.size = cmosSz;
          break;

        case KDBGER_REQ_PCI_LIST:

          // Scan PCI devices
          pKdbgerCommPkt->kdbgerRspPciListPkt.numOfPciDevice =
              kdbgerPciDetectDevice((kdbgerPciDev_t*)&pKdbgerCommPkt
                                        ->kdbgerRspPciListPkt.pciListContent);

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_PCI_LIST;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen =
              sizeof(kdbgerRspPciListPkt_t) - sizeof(kdbgerPciDev_t*) +
              pKdbgerCommPkt->kdbgerRspPciListPkt.numOfPciDevice *
                  sizeof(kdbgerPciDev_t);
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          break;

        case KDBGER_REQ_E810_LIST:

          // Copy E820
          pKdbgerCommPkt->kdbgerRspE820ListPkt.numOfE820Record =
              kdbgerE820Copy((kdbgerE820record_t*)&pKdbgerCommPkt
                                 ->kdbgerRspE820ListPkt.e820ListContent);

          // Prepare the response packet
          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_E810_LIST;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen =
              sizeof(kdbgerRspE820ListPkt_t) - sizeof(kdbgerE820record_t*) +
              pKdbgerCommPkt->kdbgerRspE820ListPkt.numOfE820Record *
                  sizeof(kdbgerE820record_t);
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          break;

        default:

          pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_NACK;
          pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof(kdbgerCommHdr_t);
          pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
          break;
      }

      // Transit state to TRAN
      kdbgerSetState(KDBGER_PKT_TRAN);

      // Start to transmit
      for (idxBuf = 0; idxBuf < pKdbgerCommPkt->kdbgerCommHdr.pktLen;)
        if (IoInByte(kdbgerPortAddr + UART_REG_LSR) & UART_LSR_TXE)
          IoOutByte(pktBuf[idxBuf++], kdbgerPortAddr + UART_REG_THR);

      if (restLen) {
        CbMemCpy(pktBuf, restBuf, restLen);
        idxBuf = restLen;
        kdbgerSetState(KDBGER_PKT_RECV);
      } else
        kdbgerSetState(KDBGER_READY);
    }
  }

done:

  // Reenable interrupt
  kdbgerIntrEnable();
}

static void kdbgerSetupBaudrate(kdbgerBaudrate_t baud) {
  int8_t tmp;

  // DLAB On
  tmp = IoInByte(kdbgerPortAddr + UART_REG_LCR);
  IoOutByte(tmp | UART_LCR_DLAB, kdbgerPortAddr + UART_REG_LCR);

  // Divisor
  IoOutByte(baud, kdbgerPortAddr + UART_REG_DLL);
  IoOutByte(0x00, kdbgerPortAddr + UART_REG_DLH);

  // DLAB Off
  IoOutByte(tmp, kdbgerPortAddr + UART_REG_LCR);
}

void kdbgerInitialization(kdbgerDebugPort_t port) {
  // Indicate INIT state
  kdbgerSetState(KDBGER_INIT);

  // Setup UART base address
  kdbgerPortAddr = port;

  // Turn off Interrupt
  kdbgerIntrDisable();

  // Setup baudrate
  kdbgerSetupBaudrate(KDBGER_BAUD_115200);

  // 8 Bits, No Parity, 1 Stop Bit
  IoOutByte(UART_STOPBIT_1 | UART_DATABIT_8, kdbgerPortAddr + UART_REG_LCR);

  // Enable FIFO
  kdbgerFifoEnable();

  // Transit to READY state
  kdbgerSetState(KDBGER_READY);

  // Disable interrupt
  IntDisable();

  // Setup interrupt handler
  IntRegInterrupt(IRQ_SERIAL0, IRQHandler(4), kdbgerIntHandler);

  // Enable interrupt
  IntEnable();

  // Turn on interrupt
  kdbgerIntrEnable();
}
