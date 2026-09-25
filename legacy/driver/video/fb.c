#include <driver/fb.h>
#include <types.h>

#if CONFIG_BOARD_RPI4
#include <arm64/mbox.h>
#endif

// 640x480x3 = 921,600 bytes
uint8_t fb_mem[FB_WIDTH * FB_HEIGHT * FB_BPP] __attribute__((aligned(4096)));
uint8_t* fb_active_mem = fb_mem;
uint64_t fb_phys_addr = (uint64_t)fb_mem;

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
        fb_active_mem[i * FB_BPP] = r;
        fb_active_mem[i * FB_BPP + 1] = g;
        fb_active_mem[i * FB_BPP + 2] = b;
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
                            fb_active_mem[offset] = r;
                            fb_active_mem[offset + 1] = g;
                            fb_active_mem[offset + 2] = b;
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
    fb_active_mem = fb_mem;
    fb_phys_addr = (uint64_t)fb_mem;

#if CONFIG_BOARD_RPI4
    {
        // Use a hardcoded physical address in 0-1GB range (e.g. 16MB)
        volatile uint32_t* mbox_buf = (volatile uint32_t*)0x1000000;
        
        mbox_buf[0] = 22 * 4; // Buffer size in bytes
        mbox_buf[1] = MBOX_REQUEST;
        
        // Tag 1: Set Phys Size
        mbox_buf[2] = MBOX_TAG_SET_PHYSICAL_W_H;
        mbox_buf[3] = 8;
        mbox_buf[4] = 8;
        mbox_buf[5] = FB_WIDTH;
        mbox_buf[6] = FB_HEIGHT;
        
        // Tag 2: Set Virt Size
        mbox_buf[7] = MBOX_TAG_SET_VIRTUAL_W_H;
        mbox_buf[8] = 8;
        mbox_buf[9] = 8;
        mbox_buf[10] = FB_WIDTH;
        mbox_buf[11] = FB_HEIGHT;
        
        // Tag 3: Set Depth
        mbox_buf[12] = MBOX_TAG_SET_DEPTH;
        mbox_buf[13] = 4;
        mbox_buf[14] = 4;
        mbox_buf[15] = 24; // 24 bpp
        
        // Tag 4: Allocate Buffer
        mbox_buf[16] = MBOX_TAG_ALLOCATE_BUFFER;
        mbox_buf[17] = 8;
        mbox_buf[18] = 8;
        mbox_buf[19] = 16; // Alignment
        mbox_buf[20] = 0;  // Placeholder for pointer
        
        // Tag 5: End
        mbox_buf[21] = 0;
        
        pl011_puts("FB: Requesting GPU Framebuffer via Mailbox...\n");
        if (mbox_call(MBOX_CH_PROP, (uint32_t*)mbox_buf) == 0) {
            uint32_t pointer = mbox_buf[19];
            uint32_t size = mbox_buf[20];
            pl011_puts("FB: GPU Framebuffer allocated at physical: ");
            print_hex(pointer);
            pl011_puts(" size: ");
            print_hex(size);
            pl011_puts("\n");
            
            uint32_t arm_phys_addr = pointer & 0x3FFFFFFF;
            
            // Check if the address is inside 0-2GB RAM (which is mapped in boot pg_dir)
            if (arm_phys_addr < 0x80000000) {
                fb_active_mem = (uint8_t*)(uint64_t)arm_phys_addr;
                fb_phys_addr = arm_phys_addr;
                pl011_puts("FB: Mailbox FB initialized successfully!\n");
            } else {
                pl011_puts("FB: WARNING: GPU address ");
                print_hex(arm_phys_addr);
                pl011_puts(" is outside mapped RAM (0-2GB)! Falling back to BSS.\n");
            }
        } else {
            pl011_puts("FB: Mailbox call failed! Falling back to BSS.\n");
        }
    }
#endif

    fb_clear(0, 0, 0);  // Black background
    // Draw "OluxOS" in white with scale 8
    fb_draw_string("OluxOS", 128, 208, 8, 255, 255, 255);

    pl011_puts("FRAMEBUFFER initialized at physical: ");
    print_hex(fb_phys_addr);
    pl011_puts("\n");
}
