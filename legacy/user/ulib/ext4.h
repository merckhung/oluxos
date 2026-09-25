#ifndef _EXT4_H_
#define _EXT4_H_

#include <types.h>

#define EXT4_SUPER_MAGIC 0xEF53
#define EXT4_EXTENTS_FL  0x80000
#define EXT4_EXT_MAGIC   0xF30A

// Superblock
typedef struct {
  uint32_t s_inodes_count;
  uint32_t s_blocks_count_lo;
  uint32_t s_r_blocks_count_lo;
  uint32_t s_free_blocks_count_lo;
  uint32_t s_free_inodes_count_lo;
  uint32_t s_first_data_block;
  uint32_t s_log_block_size;
  uint32_t s_log_cluster_size;
  uint32_t s_blocks_per_group;
  uint32_t s_clusters_per_group;
  uint32_t s_inodes_per_group;
  uint32_t s_mtime;
  uint32_t s_wtime;
  uint16_t s_mnt_count;
  uint16_t s_max_mnt_count;
  uint16_t s_magic;
  uint16_t s_state;
  uint16_t s_errors;
  uint16_t s_minor_rev_level;
  uint32_t s_lastcheck;
  uint32_t s_checkinterval;
  uint32_t s_creator_os;
  uint32_t s_rev_level;
  uint16_t s_def_resuid;
  uint16_t s_def_resgid;
  // EXT4_DYNAMIC_REV Specific fields
  uint32_t s_first_ino;
  uint16_t s_inode_size;
  uint16_t s_block_group_nr;
  uint32_t s_feature_compat;
  uint32_t s_feature_incompat;
  uint32_t s_feature_ro_compat;
  uint8_t  s_uuid[16];
  char     s_volume_name[16];
  char     s_last_mounted[64];
  uint32_t s_algorithm_usage_bitmap;
  uint8_t  s_prealloc_blocks;
  uint8_t  s_prealloc_dir_blocks;
  uint16_t s_reserved_gdt_blocks;
  uint8_t  s_journal_uuid[16];
  uint32_t s_journal_inum;
  uint32_t s_journal_dev;
  uint32_t s_last_orphan;
  uint32_t s_hash_seed[4];
  uint8_t  s_def_hash_version;
  uint8_t  s_jnl_backup_type;
  uint16_t s_desc_size;
  // ... rest of superblock can be ignored
} PACKED Ext4SuperBlock;

// Group Descriptor (32-bit fields are enough if s_desc_size is 32 or we only use low bits)
typedef struct {
  uint32_t bg_block_bitmap_lo;
  uint32_t bg_inode_bitmap_lo;
  uint32_t bg_inode_table_lo;
  uint16_t bg_free_blocks_count_lo;
  uint16_t bg_free_inodes_count_lo;
  uint16_t bg_used_dirs_count_lo;
  uint16_t bg_flags;
  uint32_t bg_exclude_bitmap_lo;
  uint16_t bg_block_bitmap_csum_lo;
  uint16_t bg_inode_bitmap_csum_lo;
  uint16_t bg_itable_unused_lo;
  uint16_t bg_checksum;
  // 64-bit fields follow if s_desc_size > 32
} PACKED Ext4GroupDesc;

// Inode
typedef struct {
  uint16_t i_mode;
  uint16_t i_uid;
  uint32_t i_size_lo;
  uint32_t i_atime;
  uint32_t i_ctime;
  uint32_t i_mtime;
  uint32_t i_dtime;
  uint16_t i_gid;
  uint16_t i_links_count;
  uint32_t i_blocks_lo;
  uint32_t i_flags;
  uint32_t i_osd1;
  union {
    uint32_t i_block[15]; // Block pointers or Extent Header
    uint8_t  i_block_bytes[60];
  };
  uint32_t i_generation;
  uint32_t i_file_acl_lo;
  uint32_t i_size_high;
  uint32_t i_obso_faddr;
  // OS dependent fields...
  uint8_t  i_osd2[12];
  uint16_t i_extra_isize;
  uint16_t i_checksum_hi;
  uint32_t i_ctime_extra;
  uint32_t i_mtime_extra;
  uint32_t i_atime_extra;
  uint32_t i_crtime;
  uint32_t i_crtime_extra;
  uint32_t i_version_hi;
  uint32_t i_projid;
} PACKED Ext4Inode;

// Extent Header
typedef struct {
  uint16_t eh_magic;
  uint16_t eh_entries;
  uint16_t eh_max;
  uint16_t eh_depth;
  uint32_t eh_generation;
} PACKED Ext4ExtentHeader;

// Extent Leaf
typedef struct {
  uint32_t ee_block;
  uint16_t ee_len;
  uint16_t ee_start_hi;
  uint32_t ee_start_lo;
} PACKED Ext4Extent;

// Extent Index
typedef struct {
  uint32_t ei_block;
  uint32_t ei_leaf_lo;
  uint16_t ei_leaf_hi;
  uint16_t ei_unused;
} PACKED Ext4ExtentIdx;

// Directory Entry
typedef struct {
  uint32_t inode;
  uint16_t rec_len;
  uint8_t  name_len;
  uint8_t  file_type;
  char     name[];
} PACKED Ext4DirEntry;


// API Functions
int ext4_init(void);
int ext4_open_file(const char* filename);
int ext4_read_file_handle(int handle, unsigned char* out_buf, unsigned int count);
int ext4_close_file(int handle);
int ext4_list_dir(const char* path, char* out_buf, unsigned int max_size);

#endif
