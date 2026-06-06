#ifndef _DRIVER_FB_H_
#define _DRIVER_FB_H_

#include <types.h>

#define FB_WIDTH 640
#define FB_HEIGHT 480
#define FB_BPP 3 // 24bpp RGB

extern uint8_t fb_mem[FB_WIDTH * FB_HEIGHT * FB_BPP];

void fb_init(void);
void fb_clear(uint8_t r, uint8_t g, uint8_t b);
void fb_draw_char(char c, int x, int y, int scale, uint8_t r, uint8_t g,
                  uint8_t b);
void fb_draw_string(const char *s, int x, int y, int scale, uint8_t r,
                    uint8_t g, uint8_t b);

#endif
