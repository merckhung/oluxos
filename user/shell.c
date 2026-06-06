#define ANY_THREAD 0xFFFFFFFF
#define UART_DRIVER_TID 1
#define RAMDISK_DRIVER_TID 2
#define FS_SERVER_TID 3
#define SHELL_TID 4

#define UART_BASE 0x09000000ULL
#define UART_DR   ((volatile unsigned int *)(UART_BASE + 0x00))
#define UART_FR   ((volatile unsigned int *)(UART_BASE + 0x18))
#define TXFF (1 << 5)
#define RXFE (1 << 4)

// Simple helpers
void user_memcpy(void *dest, const void *src, unsigned int n) {
    char *d = dest;
    const char *s = src;
    while (n--) *d++ = *s++;
}

void user_memset(void *dest, int val, unsigned int n) {
    char *d = dest;
    while (n--) *d++ = val;
}

int user_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

void user_uart_putc(char c) {
    while (*UART_FR & TXFF);
    *UART_DR = c;
}

void user_uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            user_uart_putc('\r');
        }
        user_uart_putc(*s++);
    }
}

int sys_gettid(void) {
    register long x0 __asm__("x0");
    register long x8 __asm__("x8") = 4;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

int sys_send(int dest, const void *buf, int size) {
    register long x0 __asm__("x0") = dest;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = size;
    register long x8 __asm__("x8") = 2;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

int sys_recv(int src, void *buf, int size) {
    register long x0 __asm__("x0") = src;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = size;
    register long x8 __asm__("x8") = 3;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

void* sys_map_mmio(unsigned long phys_addr) {
    register long x0 __asm__("x0") = phys_addr;
    register long x8 __asm__("x8") = 5;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return (void*)x0;
}

// Redefine puts to use IPC to UART driver
void puts(const char *s) {
    struct {
        unsigned int sender;
        unsigned int cmd;
        char data[64];
    } req;
    req.sender = sys_gettid();
    req.cmd = 0; // Write
    
    int len = 0;
    while (s[len] && len < 63) {
        req.data[len] = s[len];
        len++;
    }
    req.data[len] = '\0';
    
    sys_send(UART_DRIVER_TID, &req, 8 + len + 1);
}

void putc(char c) {
    char buf[2] = {c, '\0'};
    puts(buf);
}

char getch(void) {
    struct {
        unsigned int sender;
        unsigned int cmd;
    } req;
    req.sender = sys_gettid();
    req.cmd = 1; // Read
    
    sys_send(UART_DRIVER_TID, &req, 8);
    char c;
    sys_recv(UART_DRIVER_TID, &c, 1);
    return c;
}

// --- Ramdisk Client Helpers ---
int ramdisk_read_sector(unsigned int sector, void *buf) {
    struct {
        unsigned int sender;
        unsigned int sector;
        unsigned int write;
        unsigned char data[512];
    } req;
    
    req.sender = sys_gettid();
    req.sector = sector;
    req.write = 0;
    
    sys_send(RAMDISK_DRIVER_TID, &req, 12);
    int n = sys_recv(RAMDISK_DRIVER_TID, buf, 512);
    return n == 512 ? 0 : -1;
}

// --- FAT32 Driver ---
typedef struct {
    char name[8];
    char ext[3];
    unsigned char attr;
    unsigned char lcase;
    unsigned char creation_time_ms;
    unsigned short creation_time;
    unsigned short creation_date;
    unsigned short last_access_date;
    unsigned short first_cluster_high;
    unsigned short write_time;
    unsigned short write_date;
    unsigned short first_cluster_low;
    unsigned int file_size;
} __attribute__((packed)) FatDirEntry;

unsigned int BPB_BytePerSec;
unsigned int BPB_SecPerClus;
unsigned int BPB_RsvdSecCnt;
unsigned int BPB_NumFATs;
unsigned int BPB_FATSz32;
unsigned int BPB_RootClus;
unsigned int first_data_sector;

int fat32_init(void) {
    unsigned char buf[512];
    if (ramdisk_read_sector(0, buf) < 0) {
        puts("FAT32: Failed to read sector 0\n");
        return -1;
    }
    
    if (buf[510] != 0x55 || buf[511] != 0xAA) {
        puts("FAT32: Invalid boot signature\n");
        return -1;
    }
    
    BPB_BytePerSec = buf[11] | (buf[12] << 8);
    BPB_SecPerClus = buf[13];
    BPB_RsvdSecCnt = buf[14] | (buf[15] << 8);
    BPB_NumFATs = buf[16];
    
    unsigned int FATSz16 = buf[22] | (buf[23] << 8);
    if (FATSz16 != 0) {
        puts("FAT32: Not a FAT32 image\n");
        return -1;
    }
    
    BPB_FATSz32 = buf[36] | (buf[37] << 8) | (buf[38] << 16) | (buf[39] << 24);
    BPB_RootClus = buf[44] | (buf[45] << 8) | (buf[46] << 16) | (buf[47] << 24);
    
    first_data_sector = BPB_RsvdSecCnt + (BPB_NumFATs * BPB_FATSz32);
    return 0;
}

unsigned int cluster_to_sector(unsigned int cluster) {
    return first_data_sector + (cluster - 2) * BPB_SecPerClus;
}

unsigned int get_next_cluster(unsigned int cluster) {
    unsigned int fat_offset = cluster * 4;
    unsigned int fat_sector = BPB_RsvdSecCnt + (fat_offset / 512);
    unsigned int entry_offset = fat_offset % 512;
    
    unsigned char buf[512];
    if (ramdisk_read_sector(fat_sector, buf) < 0) {
        return 0x0FFFFFF7;
    }
    
    unsigned int next = buf[entry_offset] | (buf[entry_offset+1] << 8) | (buf[entry_offset+2] << 16) | (buf[entry_offset+3] << 24);
    return next & 0x0FFFFFFF;
}

int fat32_read_file(const char *filename, unsigned char *out_buf, unsigned int max_size) {
    unsigned char sector_buf[512];
    unsigned int current_cluster = BPB_RootClus;
    
    while (current_cluster < 0x0FFFFFF8) {
        unsigned int sector = cluster_to_sector(current_cluster);
        unsigned int s;
        for (s = 0; s < BPB_SecPerClus; s++) {
            if (ramdisk_read_sector(sector + s, sector_buf) < 0) {
                return -1;
            }
            
            FatDirEntry *entry = (FatDirEntry *)sector_buf;
            unsigned int i;
            for (i = 0; i < 512 / sizeof(FatDirEntry); i++) {
                if (entry[i].name[0] == 0x00) {
                    return -1;
                }
                if (entry[i].name[0] == 0xE5) {
                    continue;
                }
                if (entry[i].attr == 0x0F) {
                    continue;
                }
                
                int match = 1;
                int j;
                for (j = 0; j < 11; j++) {
                    if (entry[i].name[j] != filename[j]) {
                        match = 0;
                        break;
                    }
                }
                
                if (match) {
                    unsigned int file_cluster = entry[i].first_cluster_low | (entry[i].first_cluster_high << 16);
                    unsigned int file_size = entry[i].file_size;
                    
                    unsigned int bytes_to_read = file_size < max_size ? file_size : max_size;
                    unsigned int bytes_read = 0;
                    unsigned int fc = file_cluster;
                    
                    while (fc < 0x0FFFFFF8 && bytes_read < bytes_to_read) {
                        unsigned int fsector = cluster_to_sector(fc);
                        unsigned int fs;
                        for (fs = 0; fs < BPB_SecPerClus && bytes_read < bytes_to_read; fs++) {
                            unsigned char file_buf[512];
                            if (ramdisk_read_sector(fsector + fs, file_buf) < 0) {
                                return -1;
                            }
                            unsigned int chunk = bytes_to_read - bytes_read;
                            if (chunk > 512) chunk = 512;
                            
                            user_memcpy(out_buf + bytes_read, file_buf, chunk);
                            bytes_read += chunk;
                        }
                        fc = get_next_cluster(fc);
                    }
                    return bytes_read;
                }
            }
        }
        current_cluster = get_next_cluster(current_cluster);
    }
    return -1;
}

int fat32_list_dir(char *out_buf, unsigned int max_size) {
    unsigned char sector_buf[512];
    unsigned int current_cluster = BPB_RootClus;
    unsigned int offset = 0;
    
    while (current_cluster < 0x0FFFFFF8) {
        unsigned int sector = cluster_to_sector(current_cluster);
        unsigned int s;
        for (s = 0; s < BPB_SecPerClus; s++) {
            if (ramdisk_read_sector(sector + s, sector_buf) < 0) {
                return -1;
            }
            
            FatDirEntry *entry = (FatDirEntry *)sector_buf;
            unsigned int i;
            for (i = 0; i < 512 / sizeof(FatDirEntry); i++) {
                if (entry[i].name[0] == 0x00) {
                    return offset;
                }
                if (entry[i].name[0] == 0xE5) {
                    continue;
                }
                if (entry[i].attr == 0x0F) {
                    continue;
                }
                
                int len = 0;
                int k;
                for (k = 0; k < 8; k++) {
                    if (entry[i].name[k] != ' ') {
                        if (offset < max_size - 1) {
                            out_buf[offset++] = entry[i].name[k];
                        }
                    }
                }
                if (entry[i].ext[0] != ' ') {
                    if (offset < max_size - 1) {
                        out_buf[offset++] = '.';
                    }
                    for (k = 0; k < 3; k++) {
                        if (entry[i].ext[k] != ' ') {
                            if (offset < max_size - 1) {
                                out_buf[offset++] = entry[i].ext[k];
                            }
                        }
                    }
                }
                if (offset < max_size - 1) {
                    out_buf[offset++] = '\n';
                }
            }
        }
        current_cluster = get_next_cluster(current_cluster);
    }
    return offset;
}

// Helper to format filename to 8.3
void format_filename(const char* src, char* dest) {
    user_memset(dest, ' ', 11);
    int i = 0;
    int d = 0;
    while (src[i] && src[i] != '.' && d < 8) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        dest[d++] = c;
        i++;
    }
    while (src[i] && src[i] != '.') i++;
    if (src[i] == '.') i++;
    d = 8;
    while (src[i] && d < 11) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        dest[d++] = c;
        i++;
    }
}

int cmd_match(const char *cmd, const char *input) {
    int i = 0;
    while (cmd[i] && input[i] && cmd[i] == input[i]) i++;
    if (cmd[i] == '\0' && (input[i] == '\0' || input[i] == ' ')) {
        return 1;
    }
    return 0;
}

// --- Main ---
void main(void) {
    int tid = sys_gettid();
    
    if (tid == UART_DRIVER_TID) {
        sys_map_mmio(0x09000000);
        user_uart_puts("UART Driver: Initialized.\n");
        
        struct {
            unsigned int sender;
            unsigned int cmd;
            char data[64];
        } req;
        
        while (1) {
            int n = sys_recv(ANY_THREAD, &req, sizeof(req));
            if (n >= 8) {
                if (req.cmd == 0) {
                    req.data[n - 8] = '\0';
                    user_uart_puts(req.data);
                } else if (req.cmd == 1) {
                    char c;
                    while (*UART_FR & RXFE);
                    c = (char)(*UART_DR & 0xFF);
                    sys_send(req.sender, &c, 1);
                }
            }
        }
    } 
    else if (tid == RAMDISK_DRIVER_TID) {
        unsigned char *ramdisk_base = sys_map_mmio(0x48000000);
        puts("Ramdisk Driver: Initialized.\n");
        
        struct {
            unsigned int sender;
            unsigned int sector;
            unsigned int write;
            unsigned char data[512];
        } req;
        
        while (1) {
            int n = sys_recv(ANY_THREAD, &req, sizeof(req));
            if (n >= 12) {
                if (req.write == 0) {
                    unsigned char *sector_ptr = ramdisk_base + req.sector * 512;
                    sys_send(req.sender, sector_ptr, 512);
                } else {
                    unsigned int status = -1;
                    sys_send(req.sender, &status, 4);
                }
            }
        }
    } 
    else if (tid == FS_SERVER_TID) {
        puts("FS Server: Initializing FAT32...\n");
        volatile int d;
        for (d = 0; d < 1000000; d++);
        
        if (fat32_init() < 0) {
            puts("FS Server: FAT32 Init Failed!\n");
            while(1);
        }
        puts("FS Server: FAT32 Init Success.\n");
        
        struct {
            unsigned int sender;
            unsigned int cmd;
            char filename[11];
        } req;
        
        struct {
            int size;
            unsigned char data[512];
        } reply;
        
        while (1) {
            int n = sys_recv(ANY_THREAD, &req, sizeof(req));
            if (n >= 8) {
                if (req.cmd == 0) { // Read File
                    int read_bytes = fat32_read_file(req.filename, reply.data, 512);
                    reply.size = read_bytes;
                    sys_send(req.sender, &reply, sizeof(reply.size) + (read_bytes > 0 ? read_bytes : 0));
                } else if (req.cmd == 1) { // List Dir
                    int read_bytes = fat32_list_dir((char*)reply.data, 512);
                    reply.size = read_bytes;
                    sys_send(req.sender, &reply, sizeof(reply.size) + (read_bytes > 0 ? read_bytes : 0));
                }
            }
        }
    } 
    else if (tid == SHELL_TID) {
        // Interactive Shell
        volatile int d;
        for (d = 0; d < 5000000; d++);
        
        puts("\n====================================\n");
        puts(" OluxOS Userspace Shell\n");
        puts("====================================\n");
        
        char cmd_buf[64];
        int cmd_idx = 0;
        
        puts("OluxOS > ");
        
        while (1) {
            char c = getch();
            
            if (c == '\r' || c == '\n') {
                cmd_buf[cmd_idx] = '\0';
                puts("\n");
                
                if (cmd_idx > 0) {
                    // Handle command
                    if (cmd_match("help", cmd_buf)) {
                        puts("Commands:\n");
                        puts("  help       - Show this help\n");
                        puts("  ls         - List files\n");
                        puts("  cat <file> - Show file content\n");
                    } 
                    else if (cmd_match("ls", cmd_buf)) {
                        struct {
                            unsigned int sender;
                            unsigned int cmd;
                        } fs_req;
                        fs_req.sender = sys_gettid();
                        fs_req.cmd = 1; // List Dir
                        
                        sys_send(FS_SERVER_TID, &fs_req, sizeof(fs_req));
                        
                        struct {
                            int size;
                            char data[512];
                        } fs_reply;
                        
                        int n = sys_recv(FS_SERVER_TID, &fs_reply, sizeof(fs_reply));
                        if (n > 4 && fs_reply.size > 0) {
                            fs_reply.data[fs_reply.size] = '\0';
                            puts(fs_reply.data);
                        } else {
                            puts("Failed to list directory\n");
                        }
                    } 
                    else if (cmd_match("cat", cmd_buf)) {
                        // Find parameter
                        int p = 3;
                        while (cmd_buf[p] == ' ') p++;
                        if (cmd_buf[p] == '\0') {
                            puts("Usage: cat <filename>\n");
                        } else {
                            char raw_filename[32];
                            int r = 0;
                            while (cmd_buf[p] && cmd_buf[p] != ' ' && r < 31) {
                                raw_filename[r++] = cmd_buf[p++];
                            }
                            raw_filename[r] = '\0';
                            
                            char formatted[11];
                            format_filename(raw_filename, formatted);
                            
                            struct {
                                unsigned int sender;
                                unsigned int cmd;
                                char filename[11];
                            } fs_req;
                            fs_req.sender = sys_gettid();
                            fs_req.cmd = 0; // Read File
                            user_memcpy(fs_req.filename, formatted, 11);
                            
                            sys_send(FS_SERVER_TID, &fs_req, sizeof(fs_req));
                            
                            struct {
                                int size;
                                char data[512];
                            } fs_reply;
                            
                            int n = sys_recv(FS_SERVER_TID, &fs_reply, sizeof(fs_reply));
                            if (n > 4 && fs_reply.size > 0) {
                                fs_reply.data[fs_reply.size] = '\0';
                                puts(fs_reply.data);
                                puts("\n");
                            } else {
                                puts("File not found or read failed\n");
                            }
                        }
                    } 
                    else {
                        puts("Unknown command: ");
                        puts(cmd_buf);
                        puts("\n");
                    }
                }
                
                cmd_idx = 0;
                puts("OluxOS > ");
            } 
            else if (c == '\b' || c == 127) {
                if (cmd_idx > 0) {
                    cmd_idx--;
                    // Erase character on terminal
                    puts("\b \b");
                }
            } 
            else {
                if (cmd_idx < 63) {
                    cmd_buf[cmd_idx++] = c;
                    // Echo character
                    putc(c);
                }
            }
        }
    }
}
