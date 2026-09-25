/*
 * Copyright (C) 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: utils.c
 * Description:
 *	OluxOS Kernel Debugger
 *
 */
#include <fcntl.h>
#include <kdbger.h>
#include <ncurses.h>
#include <otypes.h>
#include <packet.h>
#include <panel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

int32_t connectToOluxOSKernel(kdbgerUiProperty_t* pKdbgerUiProperty) {
  // Connect to OluxOS Kernel
  return executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_CONNECT, 0, 0, NULL,
                         pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT);
}

int32_t readPciList(kdbgerUiProperty_t* pKdbgerUiProperty) {
  kdbgerRspPciListPkt_t* pKdbgerRspPciListPkt;
  int32_t i;

  // Read PCI list
  if (executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_PCI_LIST, 0, 0, NULL,
                      pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT))
    return 1;

  // Save PCI list
  pKdbgerRspPciListPkt = (kdbgerRspPciListPkt_t*)pKdbgerUiProperty->pktBuf;
  pKdbgerUiProperty->numOfPciDevice = pKdbgerRspPciListPkt->numOfPciDevice;
  pKdbgerUiProperty->pKdbgerPciDev = (kdbgerPciDev_t*)malloc(
      sizeof(kdbgerPciDev_t) * pKdbgerUiProperty->numOfPciDevice);

  if (!pKdbgerUiProperty->pKdbgerPciDev) return 1;

  memcpy(pKdbgerUiProperty->pKdbgerPciDev,
         &pKdbgerRspPciListPkt->pciListContent,
         sizeof(kdbgerPciDev_t) * pKdbgerUiProperty->numOfPciDevice);

  pKdbgerUiProperty->pKdbgerPciIds = (kdbgerPciIds_t*)malloc(
      sizeof(kdbgerPciIds_t) * pKdbgerUiProperty->numOfPciDevice);

  if (!pKdbgerUiProperty->pKdbgerPciIds) return 1;

  // Read PCI IDs
  for (i = 0; i < pKdbgerUiProperty->numOfPciDevice; i++) {
    getPciVenDevTexts((pKdbgerUiProperty->pKdbgerPciDev + i)->vendorId,
                      (pKdbgerUiProperty->pKdbgerPciDev + i)->deviceId,
                      (pKdbgerUiProperty->pKdbgerPciIds + i)->venTxt,
                      (pKdbgerUiProperty->pKdbgerPciIds + i)->devTxt,
                      pKdbgerUiProperty->pciIdsPath);
  }

  return 0;
}

kdbgerPciDev_t* getPciDevice(kdbgerUiProperty_t* pKdbgerUiProperty,
                             int32_t num) {
  if (num >= pKdbgerUiProperty->numOfPciDevice) return NULL;

  return pKdbgerUiProperty->pKdbgerPciDev + num;
}

int32_t readE820List(kdbgerUiProperty_t* pKdbgerUiProperty) {
  kdbgerRspE820ListPkt_t* pKdbgerRspE820ListPkt;

  // Read E820 list
  if (executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_E810_LIST, 0, 0, NULL,
                      pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT))
    return 1;

  // Save E820 list
  pKdbgerRspE820ListPkt = (kdbgerRspE820ListPkt_t*)pKdbgerUiProperty->pktBuf;
  pKdbgerUiProperty->numOfE820Record = pKdbgerRspE820ListPkt->numOfE820Record;
  pKdbgerUiProperty->pKdbgerE820record = (kdbgerE820record_t*)malloc(
      sizeof(kdbgerE820record_t) * pKdbgerUiProperty->numOfE820Record);

  if (!pKdbgerUiProperty->pKdbgerE820record) return 1;

  memcpy(pKdbgerUiProperty->pKdbgerE820record,
         &pKdbgerRspE820ListPkt->e820ListContent,
         sizeof(kdbgerE820record_t) * pKdbgerUiProperty->numOfE820Record);

  return 0;
}

int32_t readMemory(kdbgerUiProperty_t* pKdbgerUiProperty) {
  // Read memory
  return executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_MEM_READ,
                         pKdbgerUiProperty->kdbgerDumpPanel.byteBase,
                         KDBGER_BYTE_PER_SCREEN, NULL,
                         pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT);
}

int32_t writeMemoryByEditing(kdbgerUiProperty_t* pKdbgerUiProperty) {
  // Write memory
  return executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_MEM_WRITE,
                         pKdbgerUiProperty->kdbgerDumpPanel.byteBase +
                             pKdbgerUiProperty->kdbgerDumpPanel.byteOffset,
                         sizeof(pKdbgerUiProperty->kdbgerDumpPanel.editingBuf),
                         &pKdbgerUiProperty->kdbgerDumpPanel.editingBuf,
                         pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT);
}

int32_t readIo(kdbgerUiProperty_t* pKdbgerUiProperty) {
  // Read io
  return executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_IO_READ,
                         pKdbgerUiProperty->kdbgerDumpPanel.byteBase,
                         KDBGER_BYTE_PER_SCREEN, NULL,
                         pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT);
}

int32_t writeIoByEditing(kdbgerUiProperty_t* pKdbgerUiProperty) {
  // Write io
  return executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_IO_WRITE,
                         pKdbgerUiProperty->kdbgerDumpPanel.byteBase +
                             pKdbgerUiProperty->kdbgerDumpPanel.byteOffset,
                         sizeof(pKdbgerUiProperty->kdbgerDumpPanel.editingBuf),
                         &pKdbgerUiProperty->kdbgerDumpPanel.editingBuf,
                         pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT);
}

int32_t readIde(kdbgerUiProperty_t* pKdbgerUiProperty) {
  // Read ide
  return executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_IDE_READ,
                         pKdbgerUiProperty->kdbgerDumpPanel.byteBase,
                         KDBGER_BYTE_PER_SCREEN, NULL,
                         pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT);
}

int32_t writeIdeByEditing(kdbgerUiProperty_t* pKdbgerUiProperty) {
  // Write ide
  return executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_IDE_WRITE,
                         pKdbgerUiProperty->kdbgerDumpPanel.byteBase +
                             pKdbgerUiProperty->kdbgerDumpPanel.byteOffset,
                         sizeof(pKdbgerUiProperty->kdbgerDumpPanel.editingBuf),
                         &pKdbgerUiProperty->kdbgerDumpPanel.editingBuf,
                         pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT);
}

int32_t readCmos(kdbgerUiProperty_t* pKdbgerUiProperty) {
  // Read cmos
  return executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_CMOS_READ,
                         pKdbgerUiProperty->kdbgerDumpPanel.byteBase,
                         KDBGER_BYTE_PER_SCREEN, NULL,
                         pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT);
}

int32_t writeCmosByEditing(kdbgerUiProperty_t* pKdbgerUiProperty) {
  // Write cmos
  return executeFunction(pKdbgerUiProperty->fd, KDBGER_REQ_CMOS_WRITE,
                         pKdbgerUiProperty->kdbgerDumpPanel.byteBase +
                             pKdbgerUiProperty->kdbgerDumpPanel.byteOffset,
                         sizeof(pKdbgerUiProperty->kdbgerDumpPanel.editingBuf),
                         &pKdbgerUiProperty->kdbgerDumpPanel.editingBuf,
                         pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT);
}

int32_t readPci(kdbgerUiProperty_t* pKdbgerUiProperty) {
  kdbgerPciDev_t* pKdbgerPciDev;

  // Get PCI device
  pKdbgerPciDev = getPciDevice(pKdbgerUiProperty,
                               pKdbgerUiProperty->kdbgerDumpPanel.byteBase);
  if (!pKdbgerPciDev) return 1;

  // Read PCI
  return executeFunction(
      pKdbgerUiProperty->fd, KDBGER_REQ_PCI_READ,
      calculatePciAddress(pKdbgerPciDev->bus, pKdbgerPciDev->dev,
                          pKdbgerPciDev->fun),
      KDBGER_BYTE_PER_SCREEN, NULL, pKdbgerUiProperty->pktBuf,
      KDBGER_MAXSZ_PKT);
}

int32_t writePciByEditing(kdbgerUiProperty_t* pKdbgerUiProperty) {
  kdbgerPciDev_t* pKdbgerPciDev;

  // Get PCI device
  pKdbgerPciDev = getPciDevice(pKdbgerUiProperty,
                               pKdbgerUiProperty->kdbgerDumpPanel.byteBase);
  if (!pKdbgerPciDev) return 1;

  // Write PCI
  return executeFunction(
      pKdbgerUiProperty->fd, KDBGER_REQ_PCI_WRITE,
      calculatePciAddress(pKdbgerPciDev->bus, pKdbgerPciDev->dev,
                          pKdbgerPciDev->fun) +
          pKdbgerUiProperty->kdbgerDumpPanel.byteOffset,
      sizeof(pKdbgerUiProperty->kdbgerDumpPanel.editingBuf),
      &pKdbgerUiProperty->kdbgerDumpPanel.editingBuf, pKdbgerUiProperty->pktBuf,
      KDBGER_MAXSZ_PKT);
}

uint32_t calculatePciAddress(uint16_t bus, uint8_t dev, uint8_t func) {
  return 0x80000000 | (((uint32_t)bus) << 16) |
         ((((uint32_t)dev) & 0x1F) << 11) | ((((uint32_t)func) & 0x07) << 8);
}

static int32_t readLine(int32_t fd, int8_t* lineBuf, int32_t len) {
  int8_t buf;
  int32_t i, tabs;

  // Clear buffer
  memset(lineBuf, 0, len);

  // Reading a line
  for (i = 0, tabs = 0; read(fd, &buf, 1);) {
    if (buf == '#') {
      // Skip comment
      while (read(fd, &buf, 1) && (buf != '\n'));
      return -1;
    } else if (buf == '\n') {
      if (strlen(lineBuf)) return tabs;
      return -1;
    } else if (buf == '\t')
      tabs++;
    else {
      lineBuf[i] = buf;
      i++;
    }
  }

  return -2;
}

static int32_t compartId(uint32_t id, int8_t* lineBuf) {
  int8_t idstr[5];
  int8_t temp[5];

  // Read ID string
  strncpy(idstr, lineBuf, sizeof(idstr));

  // Convert binary to ASCII
  snprintf(temp, sizeof(temp), "%4.4x", id);

  return strncmp(temp, idstr, strlen(temp));
}

int32_t getPciVenDevTexts(uint16_t venid, uint16_t devid, int8_t* ventxt,
                          int8_t* devtxt, int8_t* pciids) {
  int32_t fd, tabs, done, findven;
  int8_t lineBuf[KDBGER_MAX_READBUF];

  // Open the PCI IDS file
  fd = open(pciids, O_RDONLY);
  if (fd < 0) return 1;

  for (findven = 0, done = 0;;) {
    // Parse PCI Database file
    switch (tabs = readLine(fd, lineBuf, KDBGER_MAX_READBUF)) {
      case 0:
        if (!compartId(venid, lineBuf)) {
          strncpy(ventxt, (lineBuf + 6), KDBGER_MAX_PCINAME);
          findven = 1;
        }
        break;

      case 1:
        if (findven && !compartId(devid, lineBuf)) {
          strncpy(devtxt, (lineBuf + 6), KDBGER_MAX_PCINAME);
          done = 1;
        }
        break;

      default:
        break;
    }

    if (done) break;

    // End of File
    if (tabs == -2) break;
  }

  // Close the file
  close(fd);

  return 0;
}
