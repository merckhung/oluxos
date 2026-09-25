/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: menu.c
 * Description:
 *  OluxOS IA32 text mode console Screen Setup Menu routines
 *
 */
#include <clib.h>
#include <driver/console.h>
#include <driver/menu.h>
#include <ia32/debug.h>
#include <ia32/io.h>
#include <types.h>

static volatile uint8_t* ScnPtr = (uint8_t*)VIDEO_TEXT_ADDR;

void MenuBackground(void) {
  int32_t i;

  for (i = 0; i < COLUMN * LINE * 2; i++) {
    if (i % 2) {
      *(ScnPtr + i) = 0x30;
    } else {
      *(ScnPtr + i) = ' ';
    }
  }
}

void MenuTitle(void) {
  int8_t* title = "OluxOS Setup Utility";
  int32_t i;

  for (i = 0; i < CbStrLen(title); i++) {
    *(ScnPtr + (i * 2) + (30 * 2)) = *(title + i);
  }
}

void MenuItem(void) {
  int8_t* item =
      "     Main     Advanced     Security     Power     Boot     Exit         "
      "        ";
  int32_t i, j, k;

  i = COLUMN * 2 * 1;
  j = CbStrLen(item) * 2 + i;

  for (k = 0; i < j; i++) {
    if (i % 2) {
      *(ScnPtr + i) = 0x17;
    } else {
      *(ScnPtr + i) = *(item + k);
      k++;
    }
  }
}

void MenuHelp(void) {
  int8_t* help =
      "  F1   Help   ||  Select Item   -/+    Change Values       F9   Setup "
      "Defaults  "
      "  Esc  Exit   <>  Select Menu   Enter  Select > Sub-Menu   F10  Save "
      "and Exit   ";
  int32_t i, j, k;

  i = COLUMN * 2 * 23;
  j = CbStrLen(help) * 2 + i;

  for (k = 0; i < j; i++) {
    if (!(i % 2)) {
      *(ScnPtr + i) = *(help + k);
      k++;
    }
  }
}

void MenuCentral(void) {
  int32_t i = COLUMN * 2;
  int32_t j = COLUMN * 23;

  for (; i < j; i++) {
    *(ScnPtr + (i * 2) + 1) = 0x70;
  }
}

void MenuInit(void) {
  MenuBackground();
  MenuTitle();
  MenuItem();
  MenuCentral();
  MenuHelp();
}
