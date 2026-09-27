#ifndef _ARM64_MBOX_H_
#define _ARM64_MBOX_H_

#include <types.h>

#define MBOX_BASE       0xFE00B880ULL

#define MBOX_READ       ((volatile uint32_t*)(MBOX_BASE + 0x00))
#define MBOX_PEEK       ((volatile uint32_t*)(MBOX_BASE + 0x10))
#define MBOX_SENDER     ((volatile uint32_t*)(MBOX_BASE + 0x14))
#define MBOX_STATUS0    ((volatile uint32_t*)(MBOX_BASE + 0x18))
#define MBOX_CONFIG0    ((volatile uint32_t*)(MBOX_BASE + 0x1C))
#define MBOX_WRITE      ((volatile uint32_t*)(MBOX_BASE + 0x20))
#define MBOX_STATUS1    ((volatile uint32_t*)(MBOX_BASE + 0x38))
#define MBOX_CONFIG1    ((volatile uint32_t*)(MBOX_BASE + 0x3C))

#define MBOX_EMPTY      0x40000000
#define MBOX_FULL       0x80000000

#define MBOX_CH_POWER   0
#define MBOX_CH_FB      1
#define MBOX_CH_VUART   2
#define MBOX_CH_VCHIQ   3
#define MBOX_CH_LEDS    4
#define MBOX_CH_BUTTONS 5
#define MBOX_CH_TOUCH   6
#define MBOX_CH_COUNT   7
#define MBOX_CH_PROP    8

// Property tags
#define MBOX_TAG_GET_FIRMWARE_REV  0x00000002
#define MBOX_TAG_GET_BOARD_MODEL    0x00010001
#define MBOX_TAG_GET_BOARD_REV      0x00010002
#define MBOX_TAG_GET_BOARD_MAC      0x00010003
#define MBOX_TAG_GET_BOARD_SERIAL   0x00010004
#define MBOX_TAG_GET_ARM_MEMORY     0x00010005
#define MBOX_TAG_GET_VC_MEMORY      0x00010006

#define MBOX_TAG_ALLOCATE_BUFFER    0x00040001
#define MBOX_TAG_RELEASE_BUFFER     0x00048001
#define MBOX_TAG_BLANK_SCREEN       0x00040002
#define MBOX_TAG_GET_PHYSICAL_W_H   0x00040003
#define MBOX_TAG_GET_VIRTUAL_W_H    0x00040004
#define MBOX_TAG_SET_PHYSICAL_W_H   0x00048003
#define MBOX_TAG_SET_VIRTUAL_W_H    0x00048004
#define MBOX_TAG_GET_DEPTH          0x00040005
#define MBOX_TAG_SET_DEPTH          0x00048005
#define MBOX_TAG_GET_PITCH          0x00040008

#define MBOX_REQUEST    0x00000000
#define MBOX_SUCCESS    0x80000000
#define MBOX_ERROR      0x80000001

void mbox_write(uint8_t channel, uint32_t data);
uint32_t mbox_read(uint8_t channel);
int mbox_call(uint8_t channel, uint32_t* buf);

#endif
