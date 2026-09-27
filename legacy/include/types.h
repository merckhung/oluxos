/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: types.h
 * Description:
 * 	None
 *
 */

#ifndef _TYPES_H_
#define _TYPES_H_

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef volatile void* PhysicalMemory;

#endif

#define FALSE 0
#define TRUE 1

#define PACKED __attribute__((packed))

#endif
