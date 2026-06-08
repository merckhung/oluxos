/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */

void IoOutByte(const uint8_t value, const uint16_t port);
uint8_t IoInByte(const uint16_t port);

void IoOutWord(const uint16_t value, const uint16_t port);
uint16_t IoInWord(const uint16_t port);

void IoOutDWord(const uint32_t value, const uint16_t port);
uint32_t IoInDWord(const uint16_t port);
