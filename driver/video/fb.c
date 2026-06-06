#include <driver/fb.h>
#include <types.h>

// 640x480x3 = 921,600 bytes
uint8_t fb_mem[FB_WIDTH * FB_HEIGHT * FB_BPP] __attribute__((aligned(4096)));

static const uint8_t font_O[] = {0x7C, 0x82, 0x82, 0x82, 0x82, 0x82, 0x7C, 0x00};
static const uint8_t font_l[] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x18, 0x00};
static const uint8_t font_u[] = {0x00, 0x00, 0x84, 0x84, 0x84, 0x8C, 0x74, 0x00};
static const uint8_t font_x[] = {0x00, 0x00, 0x84, 0x48, 0x30, 0x48, 0x84, 0x00};
static const uint8_t font_o[] = {0x00, 0x00, 0x78, 0x84, 0x84, 0x84, 0x78, 0x00};
static const uint8_t font_S[] = {0x78, 0x84, 0x80, 0x78, 0x04, 0x84, 0x78, 0x00};
static const uint8_t font_s[] = {0x00, 0x00, 0x78, 0x80, 0x70, 0x08, 0xF0, 0x00};
static const uint8_t font_space[] = {0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00};

static const uint8_t* get_font_bitmap(char c) {
    switch (c) {
        case 'O':
            return font_O;
        case 'l':
            return font_l;
        case 'u':
            return font_u;
        case 'x':
            return font_x;
        case 'o':
            return font_o;
        case 'S':
            return font_S;
        case 's':
            return font_s;
        case ' ':
            return font_space;
        default:
            return font_space;
    }
}

void fb_clear(uint8_t r, uint8_t g, uint8_t b) {
    int i;
    for (i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
        fb_mem[i * FB_BPP] = r;
        fb_mem[i * FB_BPP + 1] = g;
        fb_mem[i * FB_BPP + 2] = b;
    }
}

void fb_draw_char(char c, int x, int y, int scale, uint8_t r, uint8_t g,
                  uint8_t b) {
    const uint8_t *bitmap = get_font_bitmap(c);
    int row, col;
    for (row = 0; row < 8; row++) {
        uint8_t row_byte = bitmap[row];
        for (col = 0; col < 8; col++) {
            if (row_byte & (1 << (7 - col))) {
                int sy, sx;
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px >= 0 && px < FB_WIDTH && py >= 0 &&
                            py < FB_HEIGHT) {
                            int offset = (py * FB_WIDTH + px) * FB_BPP;
                            fb_mem[offset] = r;
                            fb_mem[offset + 1] = g;
                            fb_mem[offset + 2] = b;
                        }
                    }
                }
            }
        }
    }
}

void fb_draw_string(const char *s, int x, int y, int scale, uint8_t r,
                    uint8_t g, uint8_t b) {
    int i = 0;
    while (s[i]) {
        fb_draw_char(s[i], x + i * 8 * scale, y, scale, r, g, b);
        i++;
    }
}

void pl011_puts(const char *s);
void print_hex(uint64_t val);

void fb_init(void) {
    fb_clear(0, 0, 0);  // Black background
    // Draw "OluxOS" in white with scale 8
    fb_draw_string("OluxOS", 128, 208, 8, 255, 255, 255);

    pl011_puts("FRAMEBUFFER initialized at physical: ");
    print_hex((uint64_t)fb_mem);
    pl011_puts("\n");
}
