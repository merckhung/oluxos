#include <arm64/mbox.h>
#include <arm64/platform.h>

void pl011_puts(const char* s);
void print_hex(uint64_t val);

void mbox_write(uint8_t channel, uint32_t data) {
    // Wait until Mailbox 1 Status is not full
    while (*MBOX_STATUS1 & MBOX_FULL) {
        // Spin
    }
    // Write data combined with channel
    *MBOX_WRITE = (data & ~0xF) | (channel & 0xF);
}

uint32_t mbox_read(uint8_t channel) {
    while (1) {
        // Wait until Mailbox 0 Status is not empty
        while (*MBOX_STATUS0 & MBOX_EMPTY) {
            // Spin
        }
        uint32_t val = *MBOX_READ;
        // Check if this is the channel we want
        if ((val & 0xF) == channel) {
            return val & ~0xF;
        }
    }
}

int mbox_call(uint8_t channel, uint32_t* buf) {
    // Check buffer alignment (must be 16-byte aligned)
    if ((uint64_t)buf & 0xF) {
        pl011_puts("MBOX: Buffer not 16-byte aligned!\n");
        return -1;
    }

    uint32_t phys_addr = (uint32_t)(uint64_t)buf;
    
    // We only support 32-bit physical addresses for mailbox buffer.
    // Since RAM is in 1GB-2GB range (0x40000000 - 0x80000000), it fits in 32-bit.
    
    mbox_write(channel, phys_addr);
    
    uint32_t res = mbox_read(channel);
    if (res == phys_addr) {
        // Check if request succeeded (buffer[1] contains response code)
        if (buf[1] == MBOX_SUCCESS) {
            return 0; // Success
        } else {
            pl011_puts("MBOX: Request failed. Status=");
            print_hex(buf[1]);
            pl011_puts("\n");
            return -1;
        }
    }
    
    pl011_puts("MBOX: Unexpected response address: ");
    print_hex(res);
    pl011_puts("\n");
    return -1;
}
