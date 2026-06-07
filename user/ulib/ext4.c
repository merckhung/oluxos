#include "ext4.h"
#include "stdio.h"
#include "string.h"

extern int ramdisk_read_sector(unsigned int sector, void* buf);

static unsigned int ext4_block_size = 0;
static unsigned int ext4_inodes_per_group = 0;
static unsigned int ext4_blocks_per_group = 0;
static unsigned int ext4_inode_size = 0;
static unsigned int ext4_desc_size = 0;
static unsigned int ext4_first_data_block = 0;
static unsigned int ext4_gdt_start_block = 0;
static unsigned int ext4_num_groups = 0;

static unsigned char ext4_block_buf[4096];
static unsigned char ext4_block_buf2[4096];

static int ext4_read_block(uint32_t block, void* buf) {
  uint32_t sectors_per_block = ext4_block_size / 512;
  uint32_t start_sector = block * sectors_per_block;
  uint32_t i;
  for (i = 0; i < sectors_per_block; i++) {
    if (ramdisk_read_sector(start_sector + i, (unsigned char*)buf + i * 512) < 0) {
      return -1;
    }
  }
  return 0;
}

int ext4_init(void) {
  unsigned char sb_buf[1024];
  if (ramdisk_read_sector(2, sb_buf) < 0 || ramdisk_read_sector(3, sb_buf + 512) < 0) {
    // puts("EXT4: Failed to read superblock sectors\n");
    return -1;
  }

  Ext4SuperBlock* sb = (Ext4SuperBlock*)sb_buf;
  printf("EXT4 init: magic=%x, inodes_count=%d, blocks_count=%d\n",
         (unsigned int)sb->s_magic, (unsigned int)sb->s_inodes_count, (unsigned int)sb->s_blocks_count_lo);
  if (sb->s_magic != EXT4_SUPER_MAGIC) {
    return -1;
  }

  ext4_block_size = 1024 << sb->s_log_block_size;
  if (ext4_block_size > 4096) {
    puts("EXT4: Unsupported block size > 4KB\n");
    return -1;
  }

  ext4_inodes_per_group = sb->s_inodes_per_group;
  ext4_blocks_per_group = sb->s_blocks_per_group;
  ext4_inode_size = sb->s_inode_size;
  ext4_desc_size = sb->s_desc_size;
  if (ext4_desc_size == 0) {
    ext4_desc_size = 32;
  }
  ext4_first_data_block = sb->s_first_data_block;
  ext4_gdt_start_block = (ext4_block_size == 1024) ? 2 : 1;
  ext4_num_groups = (sb->s_inodes_count + ext4_inodes_per_group - 1) / ext4_inodes_per_group;

  printf("EXT4 config: block_size=%d, inodes_per_group=%d, inode_size=%d, desc_size=%d, num_groups=%d\n",
         ext4_block_size, ext4_inodes_per_group, ext4_inode_size, ext4_desc_size, ext4_num_groups);
  return 0;
}

static int ext4_read_inode(uint32_t inode_num, Ext4Inode* out_inode) {
  uint32_t group = (inode_num - 1) / ext4_inodes_per_group;
  uint32_t index = (inode_num - 1) % ext4_inodes_per_group;

  uint32_t desc_offset = group * ext4_desc_size;
  uint32_t gdt_block = ext4_gdt_start_block + desc_offset / ext4_block_size;
  uint32_t gdt_block_offset = desc_offset % ext4_block_size;


  if (ext4_read_block(gdt_block, ext4_block_buf) < 0) {
    puts("EXT4: Failed to read GDT block\n");
    return -1;
  }

  Ext4GroupDesc* desc = (Ext4GroupDesc*)(ext4_block_buf + gdt_block_offset);
  uint32_t itable_start_block = desc->bg_inode_table_lo;

  uint32_t inode_offset = index * ext4_inode_size;
  uint32_t itable_block = itable_start_block + inode_offset / ext4_block_size;
  uint32_t itable_block_offset = inode_offset % ext4_block_size;


  if (ext4_read_block(itable_block, ext4_block_buf) < 0) {
    puts("EXT4: Failed to read Inode Table block\n");
    return -1;
  }

  uint32_t copy_size = ext4_inode_size < sizeof(Ext4Inode) ? ext4_inode_size : sizeof(Ext4Inode);
  memcpy(out_inode, ext4_block_buf + itable_block_offset, copy_size);
  return 0;
}

static int ext4_bmap(Ext4Inode* inode, uint32_t file_block, uint32_t* out_phys_block) {
  if (!(inode->i_flags & EXT4_EXTENTS_FL)) {
    puts("EXT4: Only extents are supported\n");
    return -1;
  }

  Ext4ExtentHeader hdr;
  memcpy(&hdr, inode->i_block, sizeof(Ext4ExtentHeader));

  if (hdr.eh_magic != EXT4_EXT_MAGIC) {
    puts("EXT4: Invalid extent magic in inode\n");
    return -1;
  }

  unsigned char* ext_data = (unsigned char*)(inode->i_block + 3);
  uint32_t depth = hdr.eh_depth;

  while (1) {
    if (depth == 0) {
      Ext4Extent* ext = (Ext4Extent*)ext_data;
      uint32_t i;
      for (i = 0; i < hdr.eh_entries; i++) {
        uint32_t start_lblock = ext[i].ee_block;
        uint32_t len = ext[i].ee_len;
        if (file_block >= start_lblock && file_block < start_lblock + len) {
          uint64_t phys_start = ext[i].ee_start_lo | ((uint64_t)ext[i].ee_start_hi << 32);
          *out_phys_block = phys_start + (file_block - start_lblock);
          return 0;
        }
      }
      return -1;
    } else {
      Ext4ExtentIdx* idx = (Ext4ExtentIdx*)ext_data;
      uint32_t i;
      int found_idx = -1;
      for (i = 0; i < hdr.eh_entries; i++) {
        if (file_block >= idx[i].ei_block) {
          found_idx = i;
        } else {
          break;
        }
      }
      if (found_idx == -1) {
        return -1;
      }

      uint32_t next_block = idx[found_idx].ei_leaf_lo;

      if (ext4_read_block(next_block, ext4_block_buf2) < 0) {
        puts("EXT4: Failed to read extent block\n");
        return -1;
      }

      Ext4ExtentHeader* next_hdr = (Ext4ExtentHeader*)ext4_block_buf2;
      if (next_hdr->eh_magic != EXT4_EXT_MAGIC) {
        puts("EXT4: Invalid extent magic in block\n");
        return -1;
      }

      hdr = *next_hdr;
      ext_data = (unsigned char*)(next_hdr + 1);
      depth = hdr.eh_depth;
    }
  }
}

static int ext4_read_file_data(Ext4Inode* inode, uint32_t offset, unsigned char* buf, uint32_t count) {
  uint32_t file_size = inode->i_size_lo;
  if (offset >= file_size) {
    return 0;
  }
  if (offset + count > file_size) {
    count = file_size - offset;
  }

  uint32_t bytes_read = 0;
  while (bytes_read < count) {
    uint32_t cur_offset = offset + bytes_read;
    uint32_t lblock = cur_offset / ext4_block_size;
    uint32_t block_offset = cur_offset % ext4_block_size;
    uint32_t chunk_size = ext4_block_size - block_offset;
    if (chunk_size > count - bytes_read) {
      chunk_size = count - bytes_read;
    }

    uint32_t pblock = 0;
    if (ext4_bmap(inode, lblock, &pblock) < 0) {
      memset(buf + bytes_read, 0, chunk_size);
    } else {
      if (ext4_read_block(pblock, ext4_block_buf2) < 0) {
        puts("EXT4: Failed to read file data block\n");
        return -1;
      }
      memcpy(buf + bytes_read, ext4_block_buf2 + block_offset, chunk_size);
    }
    bytes_read += chunk_size;
  }
  return bytes_read;
}

static int ext4_lookup(const char* name, uint32_t parent_inode_num, uint32_t* out_inode_num) {
  Ext4Inode parent_inode;
  if (ext4_read_inode(parent_inode_num, &parent_inode) < 0) {
    return -1;
  }

  if ((parent_inode.i_mode & 0xF000) != 0x4000) {
    puts("EXT4: Parent is not a directory\n");
    return -1;
  }

  uint32_t file_size = parent_inode.i_size_lo;
  uint32_t block_offset = 0;

  while (block_offset < file_size) {
    uint32_t lblock = block_offset / ext4_block_size;
    uint32_t pblock = 0;
    if (ext4_bmap(&parent_inode, lblock, &pblock) < 0) {
      block_offset += ext4_block_size;
      continue;
    }

    if (ext4_read_block(pblock, ext4_block_buf) < 0) {
      puts("EXT4: Failed to read directory block\n");
      return -1;
    }

    uint32_t offset = 0;
    while (offset < ext4_block_size) {
      Ext4DirEntry* de = (Ext4DirEntry*)(ext4_block_buf + offset);
      if (de->rec_len == 0) {
        break;
      }

      if (de->inode != 0 && de->name_len > 0) {
        if (strlen(name) == de->name_len && memcmp(name, de->name, de->name_len) == 0) {
          *out_inode_num = de->inode;
          return 0;
        }
      }
      offset += de->rec_len;
    }
    block_offset += ext4_block_size;
  }
  return -1;
}

static int ext4_resolve_path(const char* path, uint32_t* out_inode_num) {
  uint32_t cur_inode = 2;
  const char* p = path;
  if (*p == '/') {
    p++;
  }
  char comp[64];

  while (*p) {
    uint32_t len = 0;
    while (*p && *p != '/') {
      if (len < sizeof(comp) - 1) {
        comp[len++] = *p;
      }
      p++;
    }
    comp[len] = '\0';

    if (len > 0) {
      uint32_t next_inode = 0;
      if (ext4_lookup(comp, cur_inode, &next_inode) < 0) {
        return -1;
      }
      cur_inode = next_inode;
    }

    if (*p == '/') {
      p++;
    }
  }

  *out_inode_num = cur_inode;
  return 0;
}

#define MAX_OPEN_FILES 8
static struct {
  int used;
  uint32_t inode_num;
  uint32_t file_size;
  uint32_t cur_pos;
} ext4_open_file_table[MAX_OPEN_FILES];

int ext4_open_file(const char* filename) {
  uint32_t inode_num;
  if (ext4_resolve_path(filename, &inode_num) < 0) {
    return -1;
  }

  Ext4Inode inode;
  if (ext4_read_inode(inode_num, &inode) < 0) {
    return -1;
  }

  if ((inode.i_mode & 0xF000) != 0x8000) {
    puts("EXT4: Not a regular file\n");
    return -1;
  }

  int i;
  for (i = 0; i < MAX_OPEN_FILES; i++) {
    if (!ext4_open_file_table[i].used) {
      ext4_open_file_table[i].used = 1;
      ext4_open_file_table[i].inode_num = inode_num;
      ext4_open_file_table[i].file_size = inode.i_size_lo;
      ext4_open_file_table[i].cur_pos = 0;
      return i + 100;
    }
  }
  return -1;
}

int ext4_read_file_handle(int handle, unsigned char* out_buf, unsigned int count) {
  int idx = handle - 100;
  if (idx < 0 || idx >= MAX_OPEN_FILES || !ext4_open_file_table[idx].used) {
    return -1;
  }

  Ext4Inode inode;
  if (ext4_read_inode(ext4_open_file_table[idx].inode_num, &inode) < 0) {
    return -1;
  }

  int read_bytes = ext4_read_file_data(&inode, ext4_open_file_table[idx].cur_pos, out_buf, count);
  if (read_bytes > 0) {
    ext4_open_file_table[idx].cur_pos += read_bytes;
  }
  return read_bytes;
}

int ext4_close_file(int handle) {
  int idx = handle - 100;
  if (idx < 0 || idx >= MAX_OPEN_FILES || !ext4_open_file_table[idx].used) {
    return -1;
  }
  ext4_open_file_table[idx].used = 0;
  return 0;
}

int ext4_list_dir(const char* path, char* out_buf, unsigned int max_size) {
  uint32_t inode_num = 2;

  if (path && path[0] != '\0') {
    if (ext4_resolve_path(path, &inode_num) < 0) {
      printf("ext4_list_dir: ext4_resolve_path failed for %s\n", path);
      return -1;
    }
  }


  Ext4Inode inode;
  if (ext4_read_inode(inode_num, &inode) < 0) {
    printf("ext4_list_dir: ext4_read_inode failed!\n");
    return -1;
  }


  if ((inode.i_mode & 0xF000) != 0x4000) {
    printf("EXT4: List dir target is not a directory. i_mode=%x\n", (unsigned int)inode.i_mode);
    return -1;
  }

  uint32_t file_size = inode.i_size_lo;
  uint32_t block_offset = 0;
  uint32_t out_offset = 0;

  while (block_offset < file_size) {
    uint32_t lblock = block_offset / ext4_block_size;
    uint32_t pblock = 0;
    if (ext4_bmap(&inode, lblock, &pblock) < 0) {
      block_offset += ext4_block_size;
      continue;
    }

    if (ext4_read_block(pblock, ext4_block_buf) < 0) {
      return -1;
    }

    uint32_t offset = 0;
    while (offset < ext4_block_size) {
      Ext4DirEntry* de = (Ext4DirEntry*)(ext4_block_buf + offset);
      if (de->rec_len == 0) {
        break;
      }

      if (de->inode != 0 && de->name_len > 0) {
        if (!(de->name_len == 1 && de->name[0] == '.') &&
            !(de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.')) {
          
          uint32_t i;
          for (i = 0; i < de->name_len; i++) {
            if (out_offset < max_size - 1) {
              out_buf[out_offset++] = de->name[i];
            }
          }
          if (out_offset < max_size - 1) {
            out_buf[out_offset++] = '\n';
          }
        }
      }
      offset += de->rec_len;
    }
    block_offset += ext4_block_size;
  }

  if (out_offset < max_size) {
    out_buf[out_offset] = '\0';
  }
  return out_offset;
}
