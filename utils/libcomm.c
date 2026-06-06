/*
 * Copyright (C) 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: libcomm.c
 * Description:
 *  None
 *
 */
#include <fcntl.h>
#include <olux.h>
#include <otypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

int32_t CbPower(int32_t x, int32_t y) {
  int32_t sum = 0;

  if (!y) {
    return 1;
  } else {
    sum += x * CbPower(x, y - 1);
  }

  return sum;
}

int8_t CbAsciiToBin(int8_t value) {
  if ((value >= '0') && (value <= '9')) {
    return (value - '0');
  }

  if ((value >= 'A') && (value <= 'F')) {
    return (value - 'A') + 10;
  }

  if ((value >= 'a') && (value <= 'f')) {
    return (value - 'a') + 10;
  }

  return 0;
}

uint32_t CbAsciiBufToBin(const int8_t* buf) {
  uint32_t i, size, cal = 0;

  if (!buf) {
    return 0;
  }

  size = strlen(buf);
  for (i = 0; buf[i]; i++) {
    cal += (CbAsciiToBin(buf[i]) * CbPower(16, size - i - 1));
  }

  return cal;
}

bool ParseOneParameter(int8_t* buf, uint32_t* first) {
  // Check length
  if (strlen(buf) > 10) {
    return FALSE;
  }

  // Check hex digits
  if (!((*buf == '0') && (*(buf + 1) == 'x'))) {
    return FALSE;
  }

  *first = CbAsciiBufToBin(buf + 2);
  return TRUE;
}

bool ParseTwoParameters(int8_t* buf, uint32_t* first, uint32_t* second) {
  int8_t* sec;

  // Check length
  if (strlen(buf) > 21) {
    return FALSE;
  }

  // Check hex digits
  if (!((*buf == '0') && (*(buf + 1) == 'x'))) {
    return FALSE;
  }

  // Search '/'
  sec = index(buf, '/');
  if (!sec) {
    return FALSE;
  }

  // Separate string into first and second
  *sec = 0;
  sec++;

  // Check hex digits
  if (!((*sec == '0') && (*(sec + 1) == 'x'))) {
    return FALSE;
  }

  // Convert ASCII to Binary
  *first = CbAsciiBufToBin(buf + 2);
  *second = CbAsciiBufToBin(sec + 2);

  return TRUE;
}

int8_t ConvertDWordToByte(uint32_t* Data, uint32_t Offset) {
  uint32_t tmp, off, bs;

  off = Offset / 4;
  bs = Offset % 4;
  if (bs) {
    tmp = *(Data + off);
    tmp = ((tmp >> (bs * 8)) & 0xFF);
    return tmp;
  }

  tmp = ((*(Data + off)) & 0xFF);
  return tmp;
}

void DumpData(int8_t* pBuf, uint32_t size, uint32_t base) {
#define BYTE_PER_LINE 16

  uint32_t i, j;
  uint8_t buf[BYTE_PER_LINE];
  int8_t unalign = 0;

  if (size % BYTE_PER_LINE) {
    unalign = 1;
  }

  printf(
      " Address | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F|   ASCII "
      "DATA   \n");
  printf(
      "------------------------------------------------------------------------"
      "---\n");

  for (i = 0; i <= size; i++) {
    if (!(i % BYTE_PER_LINE)) {
      if (i) {
        for (j = 0; j < BYTE_PER_LINE; j++) {
          if (buf[j] >= '!' && buf[j] <= '~')
            printf("%c", buf[j]);
          else
            printf(".");
        }
        printf("\n");
      }

      if (i == size) break;

      printf("%8.8X : ", i + base);
      memset(buf, ' ', sizeof(buf));
    }

    buf[i % BYTE_PER_LINE] = (uint8_t)(*(pBuf + i));
    printf("%2.2X ", buf[i % BYTE_PER_LINE] & 0xFF);
  }

  if (unalign) {
    for (j = BYTE_PER_LINE - (size % BYTE_PER_LINE) - 1; j--;) printf("   ");

    for (j = 0; j < (size % BYTE_PER_LINE); j++)
      if (buf[j] >= '!' && buf[j] <= '~')
        printf("%c", buf[j]);
      else
        printf(".");

    printf("\n");
  }

  printf(
      "------------------------------------------------------------------------"
      "---\n");
}

void DisplayInBits(uint32_t value) {
  printf(
      "\n======================================================================"
      "=========================\n");
  printf(
      "31 30 29 28 27 26 25 24|23 22 21 20 19 18 17 16|15 14 13 12 11 10 09 "
      "08|07 06 05 04 03 02 01 00\n");
  printf(
      "------------------------------------------------------------------------"
      "-----------------------\n");
  printf(
      " %d  %d  %d  %d  %d  %d  %d  %d| %d  %d  %d  %d  %d  %d  %d  %d| %d  %d "
      " %d  %d  %d  %d  %d  %d| %d  %d  %d  %d  %d  %d  %d  %d\n",
      OLUX_GET_BIT(value, 31), OLUX_GET_BIT(value, 30)

                                   ,
      OLUX_GET_BIT(value, 29), OLUX_GET_BIT(value, 28), OLUX_GET_BIT(value, 27),
      OLUX_GET_BIT(value, 26), OLUX_GET_BIT(value, 25), OLUX_GET_BIT(value, 24),
      OLUX_GET_BIT(value, 23), OLUX_GET_BIT(value, 22), OLUX_GET_BIT(value, 21),
      OLUX_GET_BIT(value, 20)

          ,
      OLUX_GET_BIT(value, 19), OLUX_GET_BIT(value, 18), OLUX_GET_BIT(value, 17),
      OLUX_GET_BIT(value, 16), OLUX_GET_BIT(value, 15), OLUX_GET_BIT(value, 14),
      OLUX_GET_BIT(value, 13), OLUX_GET_BIT(value, 12), OLUX_GET_BIT(value, 11),
      OLUX_GET_BIT(value, 10)

          ,
      OLUX_GET_BIT(value, 9), OLUX_GET_BIT(value, 8), OLUX_GET_BIT(value, 7),
      OLUX_GET_BIT(value, 6), OLUX_GET_BIT(value, 5), OLUX_GET_BIT(value, 4),
      OLUX_GET_BIT(value, 3), OLUX_GET_BIT(value, 2), OLUX_GET_BIT(value, 1),
      OLUX_GET_BIT(value, 0));

  printf(
      "========================================================================"
      "=======================\n\n");
}

void ClrScr(void) {
  int32_t ret;
  ret = write(OLUX_STD_OUT, OLUX_CLEAR_SCREEN, strlen(OLUX_CLEAR_SCREEN));
}

int8_t NonBlockReadKey(void) {
  int8_t c;

  fcntl(OLUX_STD_IN, F_SETFL, O_NONBLOCK);

  c = GetKey();

  fcntl(OLUX_STD_IN, F_SETFL, O_ASYNC);

  return c;
}

bool ReadLine(int8_t* Buf, uint32_t Length) {
  uint32_t i;
  int8_t c;

  // Clear buffer
  memset(Buf, 0, Length);
  for (i = 0; i < (Length - 1);) {
    // Read a character
    c = getchar();
    if (c == '\n') {
      return TRUE;
    }

    // Push into buffer
    Buf[i] = c;
    i++;
  }

  return FALSE;
}

int8_t GetKey(void) {
  int32_t ret;
  int8_t c;
  struct termios orig, new;

  if (tcgetattr(OLUX_STD_IN, &orig)) return 0;

  memcpy(&new, &orig, sizeof(struct termios));

  new.c_lflag &= ~(ICANON);
  new.c_cc[VMIN] = 1;
  new.c_cc[VTIME] = 0;

  if (tcsetattr(OLUX_STD_IN, TCSAFLUSH, &new)) return 0;

  ret = read(OLUX_STD_IN, &c, 1);
  if (tcsetattr(OLUX_STD_IN, TCSAFLUSH, &orig)) return 0;

  return c;
}
