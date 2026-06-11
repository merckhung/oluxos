#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// A minimal dynamic loader for OluxOS.
// This parses an ELF file, maps its PT_LOAD segments into memory,
// resolves basic relocations (if needed), and jumps to its entry point.

void *load_elf(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return NULL;
    }

    struct stat st;
    fstat(fd, &st);

    void *file_data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return NULL;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)file_data;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "Not an ELF file\n");
        return NULL;
    }

    Elf64_Phdr *phdr = (Elf64_Phdr *)((char *)file_data + ehdr->e_phoff);
    
    // Find the base address for mapping (simplification: we just mmap anonymous memory for each load segment)
    // For a real loader, we'd map according to p_vaddr.
    
    // In this simple loader, we assume PIE/relocatable, and just mmap the total size.
    uint64_t min_vaddr = (uint64_t)-1;
    uint64_t max_vaddr = 0;
    
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            if (phdr[i].p_vaddr < min_vaddr) min_vaddr = phdr[i].p_vaddr;
            if (phdr[i].p_vaddr + phdr[i].p_memsz > max_vaddr) 
                max_vaddr = phdr[i].p_vaddr + phdr[i].p_memsz;
        }
    }
    
    size_t total_size = max_vaddr - min_vaddr;
    void *load_base = mmap(NULL, total_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            void *segment_addr = (void *)((char *)load_base + (phdr[i].p_vaddr - min_vaddr));
            memcpy(segment_addr, (char *)file_data + phdr[i].p_offset, phdr[i].p_filesz);
            // BSS
            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                memset((char *)segment_addr + phdr[i].p_filesz, 0, phdr[i].p_memsz - phdr[i].p_filesz);
            }
        }
    }

    // A real loader would also process PT_DYNAMIC to apply relocations (e.g. RELA).
    // Here we jump to the entry point.
    void *entry_point = (void *)((char *)load_base + (ehdr->e_entry - min_vaddr));
    
    munmap(file_data, st.st_size);
    close(fd);
    
    return entry_point;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <executable>\n", argv[0]);
        return 1;
    }

    void *entry = load_elf(argv[1]);
    if (!entry) return 1;

    // Cast the entry point to a function pointer and call it.
    // For a real loader, we need to set up the stack with argc, argv, envp, and auxv.
    void (*app_entry)(void) = (void (*)(void))entry;
    
    printf("Jumping to %p...\n", app_entry);
    app_entry();
    
    return 0;
}
