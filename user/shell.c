#include "dirent.h"
#include "stdio.h"
#include "string.h"
#include "syscall.h"
#include "unistd.h"
#include "sys/mman.h"
#include "gles.h"
#include "gui.h"
#include "ext4.h"

#define FS_FAT32 1
#define FS_EXT4  2
static int active_fs = 0;
#if defined(__riscv)
#define UART_BASE 0x10000000ULL
#define UART_THR ((volatile unsigned char*)(UART_BASE + 0))
#define UART_RBR ((volatile unsigned char*)(UART_BASE + 0))
#define UART_LSR ((volatile unsigned char*)(UART_BASE + 5))
#define LSR_RX_READY (1 << 0)
#define LSR_TX_IDLE  (1 << 5)

void user_uart_putc(char c) {
  while ((*UART_LSR & LSR_TX_IDLE) == 0);
  *UART_THR = c;
}
#else
#if CONFIG_BOARD_RPI4
#define UART_BASE 0xFE201000ULL
#else
#define UART_BASE 0x09000000ULL
#endif
#define UART_DR ((volatile unsigned int*)(UART_BASE + 0x00))
#define UART_FR ((volatile unsigned int*)(UART_BASE + 0x18))
#define TXFF (1 << 5)
#define RXFE (1 << 4)

void user_uart_putc(char c) {
  while (*UART_FR & TXFF);
  *UART_DR = c;
}
#endif

void user_uart_puts(const char* s) {
  while (*s) {
    if (*s == '\n') {
      user_uart_putc('\r');
    }
    user_uart_putc(*s++);
  }
}

// --- Ramdisk Client Helpers ---
int ramdisk_read_sector(unsigned int sector, void* buf) {
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

  unsigned int next = buf[entry_offset] | (buf[entry_offset + 1] << 8) |
                      (buf[entry_offset + 2] << 16) |
                      (buf[entry_offset + 3] << 24);
  return next & 0x0FFFFFFF;
}

#define MAX_OPEN_FILES 8
typedef struct {
  int used;
  unsigned int first_cluster;
  unsigned int current_cluster;
  unsigned int offset;
  unsigned int size;
} OpenFileEntry;

OpenFileEntry open_file_table[MAX_OPEN_FILES];

static void format_filename(const char* src, char* dest) {
  memset(dest, ' ', 11);
  int i = 0;
  int d = 0;
  // Skip leading '/' if present
  if (src[i] == '/') i++;
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

int fat32_open_file(const char* filename) {
  char formatted[11];
  format_filename(filename, formatted);

  unsigned char sector_buf[512];
  unsigned int current_cluster = BPB_RootClus;

  while (current_cluster < 0x0FFFFFF8) {
    unsigned int sector = cluster_to_sector(current_cluster);
    unsigned int s;
    for (s = 0; s < BPB_SecPerClus; s++) {
      if (ramdisk_read_sector(sector + s, sector_buf) < 0) {
        return -1;
      }

      FatDirEntry* entry = (FatDirEntry*)sector_buf;
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
          if (entry[i].name[j] != formatted[j]) {
            match = 0;
            break;
          }
        }

        if (match) {
          int h;
          for (h = 0; h < MAX_OPEN_FILES; h++) {
            if (!open_file_table[h].used) {
              open_file_table[h].used = 1;
              open_file_table[h].first_cluster =
                  entry[i].first_cluster_low |
                  (entry[i].first_cluster_high << 16);
              open_file_table[h].current_cluster =
                  open_file_table[h].first_cluster;
              open_file_table[h].offset = 0;
              open_file_table[h].size = entry[i].file_size;
              return h;
            }
          }
          return -1;
        }
      }
    }
    current_cluster = get_next_cluster(current_cluster);
  }
  return -1;
}

int fat32_read_file_handle(int handle, unsigned char* out_buf,
                           unsigned int count) {
  if (handle < 0 || handle >= MAX_OPEN_FILES) return -1;
  OpenFileEntry* f = &open_file_table[handle];
  if (!f->used) return -1;

  unsigned int remaining = f->size - f->offset;
  if (remaining == 0) return 0;

  unsigned int bytes_to_read = count < remaining ? count : remaining;
  unsigned int bytes_read = 0;

  unsigned int bytes_per_clus = BPB_SecPerClus * 512;

  while (bytes_read < bytes_to_read) {
    unsigned int clus_offset = f->offset % bytes_per_clus;
    unsigned int sec_in_clus = clus_offset / 512;
    unsigned int sec_offset = clus_offset % 512;

    unsigned int sector = cluster_to_sector(f->current_cluster) + sec_in_clus;

    unsigned char sector_buf[512];
    if (ramdisk_read_sector(sector, sector_buf) < 0) {
      return -1;
    }

    unsigned int chunk = 512 - sec_offset;
    if (chunk > (bytes_to_read - bytes_read)) {
      chunk = bytes_to_read - bytes_read;
    }

    memcpy(out_buf + bytes_read, sector_buf + sec_offset, chunk);
    bytes_read += chunk;
    f->offset += chunk;

    if ((f->offset % bytes_per_clus) == 0) {
      unsigned int next = get_next_cluster(f->current_cluster);
      if (next >= 0x0FFFFFF8) {
        if (f->offset < f->size) {
          return -1;
        }
      }
      f->current_cluster = next;
    }
  }
  return bytes_read;
}

int fat32_close_file(int handle) {
  if (handle < 0 || handle >= MAX_OPEN_FILES) return -1;
  open_file_table[handle].used = 0;
  return 0;
}

int fat32_list_dir(char* out_buf, unsigned int max_size) {
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

      FatDirEntry* entry = (FatDirEntry*)sector_buf;
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

int parse_args(char* cmdline, char* argv[]) {
  int argc = 0;
  char* p = cmdline;
  while (*p) {
    while (*p == ' ') {
      *p = '\0';
      p++;
    }
    if (*p == '\0') break;

    argv[argc++] = p;

    while (*p && *p != ' ') {
      p++;
    }
  }
  return argc;
}

void cmd_echo(int argc, char* argv[]) {
  int i;
  for (i = 1; i < argc; i++) {
    puts(argv[i]);
    if (i < argc - 1) {
      puts(" ");
    }
  }
  puts("\n");
}

void cmd_pwd(int argc, char* argv[]) {
  char buf[128];
  if (getcwd(buf, 128)) {
    puts(buf);
    puts("\n");
  } else {
    puts("pwd: error getting current directory\n");
  }
}

void cmd_clear(int argc, char* argv[]) { puts("\033[2J\033[H"); }

void cmd_wc(int argc, char* argv[]) {
  if (argc < 2) {
    puts("Usage: wc <filename>\n");
    return;
  }
  int fd = open(argv[1], 0);
  if (fd < 0) {
    puts("wc: cannot open ");
    puts(argv[1]);
    puts("\n");
    return;
  }

  char buf[512];
  ssize_t n;
  int lines = 0;
  int words = 0;
  int bytes = 0;
  int in_word = 0;

  while ((n = read(fd, buf, 512)) > 0) {
    int i;
    for (i = 0; i < n; i++) {
      bytes++;
      char c = buf[i];
      if (c == '\n') {
        lines++;
      }
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        in_word = 0;
      } else if (!in_word) {
        in_word = 1;
        words++;
      }
    }
  }
  close(fd);

  printf(" %d  %d %d %s\n", lines, words, bytes, argv[1]);
}

void cmd_head(int argc, char* argv[]) {
  char* filename = NULL;
  int lines_to_print = 10;

  int i = 1;
  while (i < argc) {
    if (strcmp(argv[i], "-n") == 0) {
      if (i + 1 < argc) {
        lines_to_print = 0;
        char* p = argv[i + 1];
        while (*p >= '0' && *p <= '9') {
          lines_to_print = lines_to_print * 10 + (*p - '0');
          p++;
        }
        i += 2;
      } else {
        puts("head: option requires an argument -- n\n");
        return;
      }
    } else {
      filename = argv[i];
      i++;
    }
  }

  if (!filename) {
    puts("Usage: head [-n lines] <filename>\n");
    return;
  }

  int fd = open(filename, 0);
  if (fd < 0) {
    puts("head: cannot open ");
    puts(filename);
    puts("\n");
    return;
  }

  char buf[512];
  ssize_t n;
  int lines = 0;
  while (lines < lines_to_print && (n = read(fd, buf, 512)) > 0) {
    int i;
    for (i = 0; i < n; i++) {
      write(1, &buf[i], 1);
      if (buf[i] == '\n') {
        lines++;
        if (lines >= lines_to_print) {
          break;
        }
      }
    }
  }
  close(fd);
}

void cmd_grep(int argc, char* argv[]) {
  if (argc < 3) {
    puts("Usage: grep <pattern> <filename>\n");
    return;
  }

  char* pattern = argv[1];
  char* filename = argv[2];

  int fd = open(filename, 0);
  if (fd < 0) {
    puts("grep: cannot open ");
    puts(filename);
    puts("\n");
    return;
  }

  char line_buf[256];
  int line_idx = 0;
  char c;

  while (read(fd, &c, 1) > 0) {
    if (c == '\n' || c == '\r') {
      line_buf[line_idx] = '\0';
      if (line_idx > 0) {
        if (strstr(line_buf, pattern) != NULL) {
          puts(line_buf);
          puts("\n");
        }
      }
      line_idx = 0;
    } else {
      if (line_idx < 255) {
        line_buf[line_idx++] = c;
      }
    }
  }
  if (line_idx > 0) {
    line_buf[line_idx] = '\0';
    if (strstr(line_buf, pattern) != NULL) {
      puts(line_buf);
      puts("\n");
    }
  }

  close(fd);
}

void cmd_ls(int argc, char* argv[]) {
  const char* path = ".";
  if (argc > 1) {
    path = argv[1];
  }
  DIR* dir = opendir(path);
  if (!dir) {
    puts("Failed to open directory\n");
  } else {
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
      puts(entry->d_name);
      puts("\n");
    }
    closedir(dir);
  }
}

void cmd_cd(int argc, char* argv[]) {
  const char* path = "/";
  if (argc > 1) {
    path = argv[1];
  }
  if (chdir(path) < 0) {
    puts("cd: no such file or directory: ");
    puts(path);
    puts("\n");
  }
}

void cmd_cat(int argc, char* argv[]) {
  if (argc < 2) {
    puts("Usage: cat <filename>\n");
    return;
  }
  int fd = open(argv[1], 0);
  if (fd < 0) {
    puts("cat: cannot open ");
    puts(argv[1]);
    puts("\n");
    return;
  }
  char file_buf[512];
  ssize_t n;
  while ((n = read(fd, file_buf, 512)) > 0) {
    write(1, file_buf, n);
  }
  puts("\n");
  close(fd);
}

static int atoi(const char* s) {
  int val = 0;
  while (*s >= '0' && *s <= '9') {
    val = val * 10 + (*s - '0');
    s++;
  }
  return val;
}

void cmd_uname(int argc, char* argv[]) {
  puts("OluxOS arm64\n");
}

void cmd_sleep(int argc, char* argv[]) {
  if (argc < 2) {
    puts("Usage: sleep <seconds>\n");
    return;
  }
  int secs = atoi(argv[1]);
  if (secs <= 0) return;
  int i;
  for (i = 0; i < secs; i++) {
    volatile int d;
    for (d = 0; d < 100000000; d++) {
      // Spin
    }
  }
}

void cmd_tail(int argc, char* argv[]) {
  char* filename = NULL;
  int lines_to_print = 10;

  int i = 1;
  while (i < argc) {
    if (strcmp(argv[i], "-n") == 0) {
      if (i + 1 < argc) {
        lines_to_print = 0;
        char* p = argv[i + 1];
        while (*p >= '0' && *p <= '9') {
          lines_to_print = lines_to_print * 10 + (*p - '0');
          p++;
        }
        i += 2;
      } else {
        puts("tail: option requires an argument -- n\n");
        return;
      }
    } else {
      filename = argv[i];
      i++;
    }
  }

  if (!filename) {
    puts("Usage: tail [-n lines] <filename>\n");
    return;
  }

  // Pass 1: count lines
  int fd = open(filename, 0);
  if (fd < 0) {
    puts("tail: cannot open ");
    puts(filename);
    puts("\n");
    return;
  }

  char buf[512];
  ssize_t n;
  int total_lines = 0;
  while ((n = read(fd, buf, 512)) > 0) {
    int j;
    for (j = 0; j < n; j++) {
      if (buf[j] == '\n') {
        total_lines++;
      }
    }
  }
  close(fd);

  // Pass 2: print last N lines
  fd = open(filename, 0);
  if (fd < 0) {
    puts("tail: cannot open ");
    puts(filename);
    puts("\n");
    return;
  }

  int lines_to_skip = total_lines - lines_to_print;
  if (lines_to_skip < 0) {
    lines_to_skip = 0;
  }

  int current_line = 0;
  while ((n = read(fd, buf, 512)) > 0) {
    int j;
    for (j = 0; j < n; j++) {
      if (current_line >= lines_to_skip) {
        write(1, &buf[j], 1);
      }
      if (buf[j] == '\n') {
        current_line++;
      }
    }
  }
  close(fd);
}

static void find_recursive(const char* base_path) {
  DIR* dir = opendir(base_path);
  if (!dir) {
    return;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char full_path[128];
    int base_len = strlen(base_path);
    int entry_len = strlen(entry->d_name);
    if (base_len + 1 + entry_len >= 128) {
      puts("find: path too long: ");
      puts(base_path);
      puts("/");
      puts(entry->d_name);
      puts("\n");
      continue;
    }

    memcpy(full_path, base_path, base_len);
    if (full_path[base_len - 1] != '/') {
      full_path[base_len] = '/';
      memcpy(full_path + base_len + 1, entry->d_name, entry_len);
      full_path[base_len + 1 + entry_len] = '\0';
    } else {
      memcpy(full_path + base_len, entry->d_name, entry_len);
      full_path[base_len + entry_len] = '\0';
    }

    puts(full_path);
    puts("\n");

    find_recursive(full_path);
  }
  closedir(dir);
}

void cmd_find(int argc, char* argv[]) {
  const char* path = ".";
  if (argc > 1) {
    path = argv[1];
  }
  puts(path);
  puts("\n");
  find_recursive(path);
}

void draw_win1_content(Window* win) {
  draw_button(win->x + 20, win->y + 40, 100, 30, "Button");
  draw_scrollbar(win->x + win->w - 25, win->y + 30, 15, win->h - 40, 30, 40);
  gui_draw_string("Win 1 Label", win->x + 20, win->y + 90, 1, 0, 0, 0);
}

void draw_win2_content(Window* win) {
  glViewport(win->x + 10, win->y + 30, win->w - 20, win->h - 40);

  GLfixed vertices[] = {
      0, GL_ONE / 2,
      -GL_ONE / 2, -GL_ONE / 2,
      GL_ONE / 2, -GL_ONE / 2
  };
  GLubyte colors[] = {
      255, 0, 0, 255,
      0, 255, 0, 255,
      0, 0, 255, 255
  };
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);
  glVertexPointer(2, GL_FIXED, 0, vertices);
  glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_COLOR_ARRAY);

  glViewport(0, 0, FB_WIDTH, FB_HEIGHT);
}

void cmd_gui(int argc, char** argv) {
  puts("Initializing GUI...\n");
  void* fb = sys_map_fb();
  if (!fb) {
    puts("Failed to map framebuffer!\n");
    return;
  }

  glInit(fb, FB_WIDTH, FB_HEIGHT);

  static Window win1, win2;
  win_init(&win1, 1, 50, 50, 300, 200, "Window 1", draw_win1_content);
  win_init(&win2, 2, 180, 120, 300, 200, "Window 2", draw_win2_content);

  // Setup Z-order
  z_list_head = NULL;
  z_list_tail = NULL;
  z_list_add_to_front(&win1);
  z_list_add_to_front(&win2);
  win1.active = false;
  win2.active = true;
  print_z_list();

  int mx = 320;
  int my = 240;

  puts("Entering interactive GUI mode.\n");
  puts("Controls: i (up), k (down), j (left), l (right), f (click), q (quit)\n");

  // Initial draw
  glClearColorx(INT_TO_FIXED(40) / 255, INT_TO_FIXED(80) / 255,
                INT_TO_FIXED(120) / 255, GL_ONE);
  glClear(GL_COLOR_BUFFER_BIT);
  gui_draw_windows();
  draw_cursor(mx, my);

  while (1) {
    char c = getch();
    if (c == 'q') {
      break;
    }

    bool moved = false;
    bool clicked = false;

    if (c == 'i') {
      my -= 15;
      if (my < 0) my = 0;
      moved = true;
    } else if (c == 'k') {
      my += 15;
      if (my >= FB_HEIGHT) my = FB_HEIGHT - 1;
      moved = true;
    } else if (c == 'j') {
      mx -= 15;
      if (mx < 0) mx = 0;
      moved = true;
    } else if (c == 'l') {
      mx += 15;
      if (mx >= FB_WIDTH) mx = FB_WIDTH - 1;
      moved = true;
    } else if (c == 'f') {
      clicked = true;
    }

    if (moved || clicked) {
      if (clicked) {
        Window* curr = z_list_tail;
        while (curr) {
          if (mx >= curr->x && mx < curr->x + curr->w &&
              my >= curr->y && my < curr->y + curr->h) {

            // Check close button click
            if (mx >= curr->x + curr->w - 16 && mx < curr->x + curr->w - 4 &&
                my >= curr->y + 4 && my < curr->y + 16) {
              puts("Window Close Clicked!\n");
              z_list_remove(curr);
              break;
            }

            // Bring to front if not active
            if (!curr->active) {
              Window* w = z_list_head;
              while (w) {
                w->active = false;
                w = w->next;
              }
              curr->active = true;
              z_list_add_to_front(curr);
              print_z_list();
              puts("Window activated: ");
              puts(curr->title);
              puts("\n");
              break;
            }

            // Check button click inside Win1
            if (curr->id == 1) {
              int bx = curr->x + 20;
              int by = curr->y + 40;
              int bw = 100;
              int bh = 30;
              if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
                puts("Button Clicked!\n");
              }
            }
            break; // Stop propagation
          }
          curr = curr->prev;
        }
      }

      glClear(GL_COLOR_BUFFER_BIT);
      gui_draw_windows();
      draw_cursor(mx, my);
      if (clicked) {
        puts("Redraw after click completed.\n");
      }
    }
  }

  puts("Exiting GUI mode.\n");
}

void cmd_run(int argc, char* argv[]) {
  if (argc < 2) {
    puts("Usage: run <executable> [args...]\n");
    return;
  }

  int fd_loader = open("/loader.elf", 0);
  if (fd_loader < 0) {
    puts("Error: Could not open /loader.elf\n");
    return;
  }

  long loader_size = fsize(fd_loader);
  if (loader_size <= 0) {
    puts("Error: Invalid loader size\n");
    close(fd_loader);
    return;
  }

  void* loader_buf = mmap(NULL, loader_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (loader_buf == MAP_FAILED) {
    puts("Error: Failed to allocate memory for loader\n");
    close(fd_loader);
    return;
  }

  long total_read = 0;
  while (total_read < loader_size) {
    long r = read(fd_loader, (char*)loader_buf + total_read, loader_size - total_read);
    if (r <= 0) break;
    total_read += r;
  }
  close(fd_loader);
  if (total_read != loader_size) {
    puts("Error: Failed to read loader\n");
    munmap(loader_buf, loader_size);
    return;
  }

  char cmdline[256];
  cmdline[0] = '\0';
  int i;
  int offset = 0;
  for (i = 1; i < argc; i++) {
    int len = strlen(argv[i]);
    if (offset + len + 2 > 256) {
      puts("Error: Command line too long\n");
      munmap(loader_buf, loader_size);
      return;
    }
    memcpy(cmdline + offset, argv[i], len);
    offset += len;
    if (i < argc - 1) {
      cmdline[offset++] = ' ';
    }
  }
  cmdline[offset] = '\0';

  printf("Spawning loader for command: %s\n", cmdline);
  int tid = sys_spawn(loader_buf, loader_size, cmdline);
  if (tid >= 0) {
    printf("Spawned loader thread tid=%d\n", tid);
  } else {
    puts("Error: Spawn failed\n");
  }

  munmap(loader_buf, loader_size);
}

// --- Main ---
void main(void) {
  int tid = sys_gettid();

  if (tid == UART_DRIVER_TID) {
#if defined(__riscv)
    void* mapped = sys_map_mmio(0x10000000);
#elif CONFIG_BOARD_RPI4
    void* mapped = sys_map_mmio(0xFE201000);
#else
    void* mapped = sys_map_mmio(0x09000000);
#endif
    if (!mapped) {
      while (1) {
        volatile int d;
        for (d = 0; d < 1000000; d++);
      }
    }
    user_uart_puts("UART Driver: Initialized.\n");

    struct {
      unsigned int sender;
      unsigned int cmd;
      unsigned int size;
      char data[64];
    } req;

    while (1) {
      int n = sys_recv(ANY_THREAD, &req, sizeof(req));
      if (n >= 8) {
        if (req.cmd == 0 && n >= 12) {
          req.data[req.size] = '\0';
          user_uart_puts(req.data);
        } else if (req.cmd == 1) {
          char c;
#if defined(__riscv)
          while ((*UART_LSR & LSR_RX_READY) == 0);
          c = *UART_RBR;
#else
          while (*UART_FR & RXFE);
          c = (char)(*UART_DR & 0xFF);
#endif
          sys_send(req.sender, &c, 1);
        }
      }
    }
  } else if (tid == RAMDISK_DRIVER_TID) {
#if defined(__riscv)
    unsigned char* ramdisk_base = sys_map_mmio(0x88000000);
#else
    unsigned char* ramdisk_base = sys_map_mmio(0x48000000);
#endif
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
          unsigned char* sector_ptr = ramdisk_base + req.sector * 512;
          sys_send(req.sender, sector_ptr, 512);
        } else {
          unsigned int status = -1;
          sys_send(req.sender, &status, 4);
        }
      }
    }
  } else if (tid == FS_SERVER_TID) {
    volatile int d;
    for (d = 0; d < 1000000; d++);

    // Auto-detect Filesystem
    unsigned char detect_buf[512];
    if (ramdisk_read_sector(2, detect_buf) == 0 &&
        detect_buf[56] == 0x53 && detect_buf[57] == 0xEF) {
      puts("FS Server: Detected EXT4 Filesystem. Initializing...\n");
      if (ext4_init() < 0) {
        puts("FS Server: EXT4 Init Failed!\n");
        while (1);
      }
      active_fs = FS_EXT4;
      puts("FS Server: EXT4 Init Success.\n");
    } else {
      puts("FS Server: Defaulting to FAT32. Initializing...\n");
      if (fat32_init() < 0) {
        puts("FS Server: FAT32 Init Failed!\n");
        while (1);
      }
      active_fs = FS_FAT32;
      puts("FS Server: FAT32 Init Success.\n");
    }

    struct {
      unsigned int sender;
      unsigned int cmd;
      union {
        char filename[64];
        struct {
          unsigned int handle;
          unsigned int count;
        } read;
        unsigned int handle;
      } args;
    } req;

    struct {
      int size;
      unsigned char data[512];
    } reply;

    int i;
    for (i = 0; i < MAX_OPEN_FILES; i++) {
      open_file_table[i].used = 0;
    }

    while (1) {
      int n = sys_recv(ANY_THREAD, &req, sizeof(req));
      if (n >= 8) {
        // Ensure null-termination of filename/path argument
        unsigned int arg_len = n - 8;
        if (arg_len < 64) {
          req.args.filename[arg_len] = '\0';
        } else {
          req.args.filename[63] = '\0';
        }

        if (req.cmd == 1) {  // List Dir
          int read_bytes = -1;
          if (active_fs == FS_FAT32) {
            read_bytes = fat32_list_dir((char*)reply.data, 512);
          } else if (active_fs == FS_EXT4) {
            read_bytes = ext4_list_dir(req.args.filename, (char*)reply.data, 512);
          }
          reply.size = read_bytes;
          sys_send(req.sender, &reply,
                   sizeof(reply.size) + (read_bytes > 0 ? read_bytes : 0));
        } else if (req.cmd == 2) {  // Open File
          int handle = -1;
          if (active_fs == FS_FAT32) {
            handle = fat32_open_file(req.args.filename);
          } else if (active_fs == FS_EXT4) {
            handle = ext4_open_file(req.args.filename);
          }
          reply.size = handle;
          sys_send(req.sender, &reply, 4);
        } else if (req.cmd == 3) {  // Read File Handle
          int read_bytes = -1;
          if (req.args.read.handle >= 100) {
            read_bytes = ext4_read_file_handle(
                req.args.read.handle, reply.data, req.args.read.count);
          } else {
            read_bytes = fat32_read_file_handle(
                req.args.read.handle, reply.data, req.args.read.count);
          }
          reply.size = read_bytes;
          sys_send(req.sender, &reply,
                   sizeof(reply.size) + (read_bytes > 0 ? read_bytes : 0));
        } else if (req.cmd == 4) {  // Close File
          int status = -1;
          if (req.args.handle >= 100) {
            status = ext4_close_file(req.args.handle);
          } else {
            status = fat32_close_file(req.args.handle);
          }
          reply.size = status;
          sys_send(req.sender, &reply, 4);
        } else if (req.cmd == 5) {  // Get File Size
          int size = -1;
          if (req.args.handle < 100) {
            if (open_file_table[req.args.handle].used) {
              size = open_file_table[req.args.handle].size;
            }
          }
          reply.size = size;
          sys_send(req.sender, &reply, 4);
        }
      }
    }

  } else if (tid == SHELL_TID) {
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
          char* argv[16];
          int argc = parse_args(cmd_buf, argv);
          if (argc > 0) {
            if (strcmp(argv[0], "help") == 0) {
              puts("Commands:\n");
              puts("  help                - Show this help\n");
              puts("  ls [dir]            - List files\n");
              puts("  cd [dir]            - Change directory\n");
              puts("  cat <file>          - Show file content\n");
              puts("  echo [args...]      - Print arguments\n");
              puts("  pwd                 - Print working directory\n");
              puts("  clear               - Clear screen\n");
              puts("  wc <file>           - Count lines, words, and bytes\n");
              puts("  head [-n N] <file>  - Show first N lines\n");
              puts("  grep <pattern> <file>- Search for pattern in file\n");
              puts("  gui                 - Launch GUI system\n");
              puts("  tail [-n N] <file>  - Show last N lines\n");
              puts("  find [dir]          - Find files recursively\n");
              puts("  uname               - Show system info\n");
              puts("  sleep <seconds>     - Sleep for N seconds\n");
              puts("  run <file> [args...] - Run ELF binary via loader\n");
            } else if (strcmp(argv[0], "ls") == 0) {
              cmd_ls(argc, argv);
            } else if (strcmp(argv[0], "cd") == 0) {
              cmd_cd(argc, argv);
            } else if (strcmp(argv[0], "cat") == 0) {
              cmd_cat(argc, argv);
            } else if (strcmp(argv[0], "echo") == 0) {
              cmd_echo(argc, argv);
            } else if (strcmp(argv[0], "pwd") == 0) {
              cmd_pwd(argc, argv);
            } else if (strcmp(argv[0], "clear") == 0) {
              cmd_clear(argc, argv);
            } else if (strcmp(argv[0], "wc") == 0) {
              cmd_wc(argc, argv);
            } else if (strcmp(argv[0], "head") == 0) {
              cmd_head(argc, argv);
            } else if (strcmp(argv[0], "grep") == 0) {
              cmd_grep(argc, argv);
            } else if (strcmp(argv[0], "gui") == 0) {
              cmd_gui(argc, argv);
            } else if (strcmp(argv[0], "tail") == 0) {
              cmd_tail(argc, argv);
            } else if (strcmp(argv[0], "find") == 0) {
              cmd_find(argc, argv);
            } else if (strcmp(argv[0], "uname") == 0) {
              cmd_uname(argc, argv);
            } else if (strcmp(argv[0], "sleep") == 0) {
              cmd_sleep(argc, argv);
            } else if (strcmp(argv[0], "run") == 0) {
              cmd_run(argc, argv);
            } else if (strcmp(argv[0], "busybox") == 0) {
              static unsigned char exec_buf[2000000];
              int fd = open("busybox", 0);
              if (fd >= 0) {
                  puts("Loading busybox...\n");
                  int total_read = 0;
                  while (1) {
                      int r = read(fd, exec_buf + total_read, 512);
                      if (r <= 0) break;
                      total_read += r;
                  }
                  close(fd);
                  if (total_read > 0) {
                      puts("Spawning busybox...\n");
                      int tid = sys_spawn(exec_buf, total_read, NULL);
                      if (tid >= 0) {
                          puts("Spawned busybox successfully.\n");
                      } else {
                          puts("Spawn failed.\n");
                      }
                  }
              } else {
                  puts("Could not open busybox file.\n");
              }
            } else {
              puts("Unknown command: ");
              puts(argv[0]);
              puts("\n");
            }
          }
        }

        cmd_idx = 0;
        puts("OluxOS > ");
      } else if (c == '\b' || c == 127) {
        if (cmd_idx > 0) {
          cmd_idx--;
          // Erase character on terminal
          puts("\b \b");
        }
      } else {
        if (cmd_idx < 63) {
          cmd_buf[cmd_idx++] = c;
          // Echo character
          putc(c);
        }
      }
    }
  }
}
