#include "unistd.h"
#include "sys/mman.h"
#include "stdio.h"
#include "string.h"
#include "elf.h"

#if defined(__riscv) && (__riscv_xlen == 32)
#define Elf_Ehdr Elf32_Ehdr
#define Elf_Phdr Elf32_Phdr
#define uint_t uint32_t
#else
#define Elf_Ehdr Elf64_Ehdr
#define Elf_Phdr Elf64_Phdr
#define uint_t uint64_t
#endif

#if defined(__arm64__) || defined(__aarch64__)
void jump_to_elf(void* entry, void* stack) {
  __asm__ volatile(
      "mov sp, %1\n"
      "mov x0, #0\n"
      "br %0\n"
      :
      : "r"(entry), "r"(stack)
      : "x0", "memory"
  );
}
#elif defined(__riscv)
void jump_to_elf(void* entry, void* stack) {
  __asm__ volatile(
      "mv sp, %1\n"
      "li a0, 0\n"
      "jr %0\n"
      :
      : "r"(entry), "r"(stack)
      : "a0", "memory"
  );
}
#else
#error "Unsupported architecture"
#endif

void debug_print_hex(const char* label, uint_t val) {
  puts("[Loader debug] ");
  puts(label);
  puts("=0x");
  int i;
  for (i = (sizeof(uint_t)*2 - 1); i >= 0; i--) {
    int digit = (val >> (i * 4)) & 0xF;
    char c = digit < 10 ? '0' + digit : 'A' + digit - 10;
    putc(c);
  }
  puts("\n");
}

void* setup_stack(void* stack_top, int argc, char* argv[], uint_t phdr, uint_t phent, uint_t phnum, uint_t entry) {
  // We use 1KB space at the top of the stack for argv/argc/auxv
  uint_t* ustack = (uint_t*)((uint_t)stack_top - 0x400);
  uint_t user_sp = (uint_t)stack_top - 0x400;
  
  ustack[0] = argc;
  int i;
  // Place strings starting at offset 512 bytes from user_sp
  uint_t str_offset = 512;
  for (i = 0; i < argc; i++) {
    ustack[1 + i] = user_sp + str_offset;
    int len = strlen(argv[i]);
    memcpy((char*)((uint_t)ustack + str_offset), argv[i], len + 1);
    str_offset += len + 1;
    // Align to 8 bytes (or 4 bytes on 32-bit, but 8 is safe for both)
    str_offset = (str_offset + 7) & ~7;
  }
  ustack[1 + argc] = 0; // NULL terminator of argv
  
  // Place "dummy=" env var string
  uint_t env_str_offset = str_offset;
  memcpy((char*)((uint_t)ustack + env_str_offset), "dummy=", 7);
  str_offset += 7;
  str_offset = (str_offset + 7) & ~7;

  // Place 16 bytes of random data
  uint_t random_offset = str_offset;
  memcpy((char*)((uint_t)ustack + random_offset), "1234567890abcdef", 16);
  str_offset += 16;
  str_offset = (str_offset + 7) & ~7;

  ustack[2 + argc] = user_sp + env_str_offset; // envp[0]
  ustack[3 + argc] = 0; // envp[1] (NULL)
  
  // Auxv
  int aux_idx = 4 + argc;
  ustack[aux_idx++] = 3; // AT_PHDR
  ustack[aux_idx++] = phdr;
  ustack[aux_idx++] = 4; // AT_PHENT
  ustack[aux_idx++] = phent;
  ustack[aux_idx++] = 5; // AT_PHNUM
  ustack[aux_idx++] = phnum;
  ustack[aux_idx++] = 9; // AT_ENTRY
  ustack[aux_idx++] = entry;
  ustack[aux_idx++] = 25; // AT_RANDOM
  ustack[aux_idx++] = user_sp + random_offset;
  ustack[aux_idx++] = 6; // AT_PAGESZ
  ustack[aux_idx++] = 4096;
  ustack[aux_idx++] = 0; // AT_NULL
  ustack[aux_idx++] = 0;
  
  return (void*)user_sp;
}

void* load_elf(const char* filename, uint_t* phdr_out, uint_t* phent_out, uint_t* phnum_out) {
  printf("[Loader] Opening ELF: %s\n", filename);
  int fd = open(filename, 0);
  if (fd < 0) {
    printf("[Loader] Open failed!\n");
    return NULL;
  }

  long size = fsize(fd);
  if (size <= 0) {
    printf("[Loader] Invalid file size: %d\n", size);
    close(fd);
    return NULL;
  }
  printf("[Loader] File size: %d bytes\n", size);

  // Allocate memory to read the ELF file
  void* file_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (file_data == MAP_FAILED) {
    printf("[Loader] mmap for file data failed!\n");
    close(fd);
    return NULL;
  }

  long total_read = 0;
  while (total_read < size) {
    long r = read(fd, (char*)file_data + total_read, size - total_read);
    if (r <= 0) break;
    total_read += r;
  }
  if (total_read != size) {
    printf("[Loader] Read failed, read %d bytes instead of %d!\n", total_read, size);
    munmap(file_data, size);
    close(fd);
    return NULL;
  }

  Elf_Ehdr* ehdr = (Elf_Ehdr*)file_data;
  if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
    printf("[Loader] Not a valid ELF file!\n");
    munmap(file_data, size);
    close(fd);
    return NULL;
  }

  Elf_Phdr* phdr = (Elf_Phdr*)((char*)file_data + ehdr->e_phoff);

  // Find range of loadable segments
  uint_t min_vaddr = (uint_t)-1;
  uint_t max_vaddr = 0;
  int i;
  for (i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      if (phdr[i].p_vaddr < min_vaddr) min_vaddr = phdr[i].p_vaddr;
      if (phdr[i].p_vaddr + phdr[i].p_memsz > max_vaddr)
        max_vaddr = phdr[i].p_vaddr + phdr[i].p_memsz;
    }
  }

  if (min_vaddr == (uint_t)-1 || max_vaddr == 0) {
    printf("[Loader] No PT_LOAD segments found!\n");
    munmap(file_data, size);
    close(fd);
    return NULL;
  }

  size_t total_size = max_vaddr - min_vaddr;
  printf("[Loader] Mapping program segments: VA 0x%x - 0x%x (size 0x%x)\n",
         (unsigned long)min_vaddr, (unsigned long)max_vaddr, (unsigned long)total_size);

  // Allocate memory at the target virtual address!
  // If the binary is statically compiled at a fixed address, we map it exactly there.
  void* load_base = mmap((void*)min_vaddr, total_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (load_base == MAP_FAILED || (uint_t)load_base != min_vaddr) {
    printf("[Loader] Failed to map at compiled virtual address 0x%x! got 0x%x\n",
           (unsigned long)min_vaddr, load_base);
    munmap(file_data, size);
    close(fd);
    return NULL;
  }

  // Copy segments into mapped virtual memory
  for (i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      void* segment_addr = (void*)((char*)load_base + (phdr[i].p_vaddr - min_vaddr));
      printf("[Loader] Copying segment: VA 0x%x (size 0x%x)\n", segment_addr, (unsigned long)phdr[i].p_filesz);
      memcpy(segment_addr, (char*)file_data + phdr[i].p_offset, phdr[i].p_filesz);
      // Clear BSS segment
      if (phdr[i].p_memsz > phdr[i].p_filesz) {
        memset((char*)segment_addr + phdr[i].p_filesz, 0, phdr[i].p_memsz - phdr[i].p_filesz);
      }
    }
  }

  *phdr_out = min_vaddr + ehdr->e_phoff;
  *phent_out = ehdr->e_phentsize;
  *phnum_out = ehdr->e_phnum;

  void* entry_point = (void*)((char*)load_base + (ehdr->e_entry - min_vaddr));
  
  // Set heap break to end of loaded ELF BSS
  uint_t heap_start = (max_vaddr + 4095) & ~4095;
  __asm__ volatile(
      "mov x0, %0\n"
      "mov x8, #214\n"
      "svc #0\n"
      :: "r"(heap_start) : "x0", "x8"
  );

  // Free temporary file buffer
  munmap(file_data, size);
  close(fd);
  
  return entry_point;
}

int main(int argc, char* argv[]) {
  register uint_t current_sp __asm__("sp");
  debug_print_hex("loader_sp", current_sp);
  debug_print_hex("argc", argc);
  debug_print_hex("argv", (uint_t)argv);
  if (argc >= 1) {
    debug_print_hex("argv[0]", (uint_t)argv[0]);
    if (argv[0]) {
      puts("[Loader debug] argv[0] string: ");
      puts(argv[0]);
      puts("\n");
    }
  }
  if (argc >= 2) {
    debug_print_hex("argv[1]", (uint_t)argv[1]);
  }

  if (argc < 2) {
    printf("Usage: %s <space-separated command line>\n", argv[0]);
    return 1;
  }

  char* cmdline = argv[1];
  printf("[Loader] Parsing command line: %s\n", cmdline);

  char* tokens[16];
  int token_count = 0;
  char* p = cmdline;
  while (*p) {
    // Skip spaces
    while (*p == ' ') p++;
    if (!*p) break;
    tokens[token_count++] = p;
    if (token_count >= 16) break;
    // Find end of token
    while (*p && *p != ' ') p++;
    if (*p == ' ') {
      *p = '\0';
      p++;
    }
  }

  if (token_count == 0) {
    printf("[Loader] No executable specified!\n");
    return 1;
  }

  const char* target_elf = tokens[0];
  uint_t phdr = 0, phent = 0, phnum = 0;
  void* entry = load_elf(target_elf, &phdr, &phent, &phnum);
  if (!entry) {
    printf("[Loader] Loading failed!\n");
    return 1;
  }

  // We set up the stack at 0x00800000 - 0x1000 (which is 0x7FF000)
  void* new_sp = setup_stack((void*)0x007FF000, token_count, tokens, phdr, phent, phnum, (uint_t)entry);

  printf("[Loader] Jumping to entry 0x%x with stack 0x%x...\n", entry, new_sp);
  
  jump_to_elf(entry, new_sp);

  return 0; // Should not be reached
}
