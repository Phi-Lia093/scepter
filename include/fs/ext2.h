#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include <stddef.h>

/* stat_t is defined in fs/fs.h (included by ext2.c); forward declaration
 * suffices for the function prototypes below. */
typedef struct stat stat_t;

/* ============================================================================
 * ext2 Filesystem Structures
 *
 * Based on the Second Extended Filesystem on-disk format (Remy Card,
 * Stephen Tweedie, Theodore Ts'o).  All multi-byte values are little-endian,
 * which matches the i386 CPU so the structures below can be overlaid on disk
 * buffers directly (same approach as the minix3 driver).
 * ============================================================================ */

/* Superblock magic */
#define EXT2_SUPER_MAGIC 0xEF53

/* Special inode numbers */
#define EXT2_BAD_INO        1
#define EXT2_ROOT_INO       2
#define EXT2_ACL_IDX_INO    3
#define EXT2_ACL_DATA_INO   4
#define EXT2_BOOT_LOADER_INO 5
#define EXT2_UNDEL_DIR_INO  6
#define EXT2_RESIZE_INO     7
#define EXT2_LOSTFOUND_INO  11

/* Filesystem states (s_state) */
#define EXT2_VALID_FS 0x0001   /* cleanly unmounted */
#define EXT2_ERROR_FS 0x0002   /* errors detected */

/* Errors behaviour (s_errors) */
#define EXT2_ERRORS_CONTINUE 1
#define EXT2_ERRORS_RO       2
#define EXT2_ERRORS_PANIC    3

/* Superblock revision levels */
#define EXT2_GOOD_OLD_REV    0
#define EXT2_DYNAMIC_REV     1

/* Default inode size and first inode for rev 0 filesystems */
#define EXT2_GOOD_OLD_INODE_SIZE 128
#define EXT2_GOOD_OLD_FIRST_INO  11

/* Max filename length */
#define EXT2_NAME_LEN 255

/* Number of direct block pointers in the inode */
#define EXT2_NDIR_BLOCKS 12

/* ============================================================================
 * File type and permission bits (i_mode)
 * ============================================================================ */
#define EXT2_S_IFMT   0xF000  /* File type mask */
#define EXT2_S_IFSOCK 0xC000  /* Socket */
#define EXT2_S_IFLNK  0xA000  /* Symbolic link */
#define EXT2_S_IFREG  0x8000  /* Regular file */
#define EXT2_S_IFBLK  0x6000  /* Block device */
#define EXT2_S_IFDIR  0x4000  /* Directory */
#define EXT2_S_IFCHR  0x2000  /* Character device */
#define EXT2_S_IFIFO  0x1000  /* FIFO */

#define EXT2_S_ISUID  0x0800  /* Set UID */
#define EXT2_S_ISGID  0x0400  /* Set GID */
#define EXT2_S_ISVTX  0x0200  /* Sticky bit */

#define EXT2_S_IRWXU  0x01C0  /* User RWX */
#define EXT2_S_IRUSR  0x0100  /* User read */
#define EXT2_S_IWUSR  0x0080  /* User write */
#define EXT2_S_IXUSR  0x0040  /* User execute */
#define EXT2_S_IRWXG  0x0038  /* Group RWX */
#define EXT2_S_IRGRP  0x0020  /* Group read */
#define EXT2_S_IWGRP  0x0010  /* Group write */
#define EXT2_S_IXGRP  0x0008  /* Group execute */
#define EXT2_S_IRWXO  0x0007  /* Other RWX */
#define EXT2_S_IROTH  0x0004  /* Other read */
#define EXT2_S_IWOTH  0x0002  /* Other write */
#define EXT2_S_IXOTH  0x0001  /* Other execute */

#define EXT2_ISREG(m)  (((m) & EXT2_S_IFMT) == EXT2_S_IFREG)
#define EXT2_ISDIR(m)  (((m) & EXT2_S_IFMT) == EXT2_S_IFDIR)
#define EXT2_ISCHR(m)  (((m) & EXT2_S_IFMT) == EXT2_S_IFCHR)
#define EXT2_ISBLK(m)  (((m) & EXT2_S_IFMT) == EXT2_S_IFBLK)
#define EXT2_ISFIFO(m) (((m) & EXT2_S_IFMT) == EXT2_S_IFIFO)
#define EXT2_ISSOCK(m) (((m) & EXT2_S_IFMT) == EXT2_S_IFSOCK)
#define EXT2_ISLNK(m)  (((m) & EXT2_S_IFMT) == EXT2_S_IFLNK)

/* ============================================================================
 * Feature flags
 * ============================================================================ */

/* Compatible (safe for old drivers to ignore) */
#define EXT2_FEATURE_COMPAT_DIR_PREALLOC  0x0001
#define EXT2_FEATURE_COMPAT_IMAGIC_INODES 0x0002
#define EXT2_FEATURE_COMPAT_HAS_JOURNAL   0x0004   /* ext3 - refuse */
#define EXT2_FEATURE_COMPAT_EXT_ATTR      0x0008
#define EXT2_FEATURE_COMPAT_RESIZE_INODE  0x0010
#define EXT2_FEATURE_COMPAT_DIR_INDEX     0x0020
#define EXT2_FEATURE_COMPAT_LAZY_BG       0x0040

/* Incompatible (old drivers must refuse to mount) */
#define EXT2_FEATURE_INCOMPAT_COMPRESSION 0x0001
#define EXT2_FEATURE_INCOMPAT_FILETYPE    0x0002   /* dirents carry file_type */
#define EXT2_FEATURE_INCOMPAT_RECOVER     0x0004   /* needs journal replay */
#define EXT2_FEATURE_INCOMPAT_JOURNAL_DEV 0x0008
#define EXT2_FEATURE_INCOMPAT_META_BG     0x0010
#define EXT2_FEATURE_INCOMPAT_EXTENTS     0x0040   /* ext4 extents */
#define EXT2_FEATURE_INCOMPAT_64BIT       0x0080
#define EXT2_FEATURE_INCOMPAT_MMP         0x0100
#define EXT2_FEATURE_INCOMPAT_FLEX_BG     0x0200

/* Read-only compatible */
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER   0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE     0x0002
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR      0x0004
#define EXT2_FEATURE_RO_COMPAT_HUGE_FILE      0x0008
#define EXT2_FEATURE_RO_COMPAT_GDT_CSUM       0x0010
#define EXT2_FEATURE_RO_COMPAT_DIR_NLINK      0x0020
#define EXT2_FEATURE_RO_COMPAT_EXTRA_ISIZE    0x0040
#define EXT2_FEATURE_RO_COMPAT_QUOTA          0x0100
#define EXT2_FEATURE_RO_COMPAT_BIGALLOC       0x0200
#define EXT2_FEATURE_RO_COMPAT_METADATA_CSUM  0x0400

/* ============================================================================
 * Directory entry file_type values (EXT2_FEATURE_INCOMPAT_FILETYPE)
 * ============================================================================ */
#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

/* ============================================================================
 * On-Disk Structures
 * ============================================================================ */

/**
 * Superblock - always located at byte offset 1024 from the start of the
 * filesystem (which is also the start of the partition).
 */
struct ext2_super_block {
    uint32_t s_inodes_count;        /* Total inode count */
    uint32_t s_blocks_count;        /* Total block count */
    uint32_t s_r_blocks_count;      /* Blocks reserved for superuser */
    uint32_t s_free_blocks_count;   /* Free blocks */
    uint32_t s_free_inodes_count;   /* Free inodes */
    uint32_t s_first_data_block;    /* 0 (blocks > 1KB) or 1 (1KB blocks) */
    uint32_t s_log_block_size;      /* block_size = 1024 << s_log_block_size */
    uint32_t s_log_frag_size;       /* fragment size (always = block size) */
    uint32_t s_blocks_per_group;    /* Blocks per block group */
    uint32_t s_frags_per_group;     /* Fragments per group */
    uint32_t s_inodes_per_group;    /* Inodes per group */
    uint32_t s_mtime;               /* Mount time */
    uint32_t s_wtime;               /* Write time */
    uint16_t s_mnt_count;           /* Mount count */
    uint16_t s_max_mnt_count;       /* Max mounts before fsck */
    uint16_t s_magic;               /* 0xEF53 */
    uint16_t s_state;               /* EXT2_VALID_FS / EXT2_ERROR_FS */
    uint16_t s_errors;              /* Behaviour on error */
    uint16_t s_minor_rev_level;     /* Minor revision */
    uint32_t s_lastcheck;           /* Last fsck time */
    uint32_t s_checkinterval;       /* Max time between fscks */
    uint32_t s_creator_os;          /* OS that created the fs */
    uint32_t s_rev_level;           /* 0 = good old, 1 = dynamic */
    uint16_t s_def_resuid;          /* Default reserved uid */
    uint16_t s_def_resgid;          /* Default reserved gid */
    /* ---- Fields below only valid for rev >= 1 ---- */
    uint32_t s_first_ino;           /* First non-reserved inode */
    uint16_t s_inode_size;          /* On-disk inode size */
    uint16_t s_block_group_nr;      /* Block group hosting this superblock */
    uint32_t s_feature_compat;      /* Compatible feature set */
    uint32_t s_feature_incompat;    /* Incompatible feature set */
    uint32_t s_feature_ro_compat;   /* Read-only compatible feature set */
    uint8_t  s_uuid[16];            /* 128-bit filesystem UUID */
    char     s_volume_name[16];     /* Volume label */
    char     s_last_mounted[64];    /* Path where last mounted */
    uint32_t s_algorithm_usage_bitmap; /* Compression algorithm bitmap */
    uint8_t  s_prealloc_blocks;     /* Preallocation block count */
    uint8_t  s_prealloc_dir_blocks; /* Preallocation dir block count */
    uint16_t s_padding1;
    uint32_t s_reserved[204];       /* Padding to 1024 bytes */
} __attribute__((packed));

/**
 * Block group descriptor - one per block group, all loaded into memory at
 * mount time.  The table lives in the block(s) right after the superblock
 * (block s_first_data_block + 1).
 */
struct ext2_group_desc {
    uint32_t bg_block_bitmap;       /* Block number of block bitmap */
    uint32_t bg_inode_bitmap;       /* Block number of inode bitmap */
    uint32_t bg_inode_table;        /* Block number of first inode table block */
    uint16_t bg_free_blocks_count;  /* Free blocks in this group */
    uint16_t bg_free_inodes_count;  /* Free inodes in this group */
    uint16_t bg_used_dirs_count;    /* Directory count in this group */
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed));

/**
 * Inode - 128 bytes of base data; on-disk stride is s_inode_size
 * (128 for rev 0, typically 128 or 256 for rev 1).
 */
struct ext2_inode {
    uint16_t i_mode;                /* Type + permission bits */
    uint16_t i_uid;                 /* Owner uid */
    uint32_t i_size;                /* Size in bytes */
    uint32_t i_atime;               /* Access time */
    uint32_t i_ctime;               /* Change time */
    uint32_t i_mtime;               /* Modification time */
    uint32_t i_dtime;               /* Deletion time */
    uint16_t i_gid;                 /* Owner gid */
    uint16_t i_links_count;         /* Hard link count */
    uint32_t i_blocks;              /* Blocks in 512-byte units */
    uint32_t i_flags;               /* File flags */
    uint32_t i_osd1;                /* OS dependent (high device bits) */
    uint32_t i_block[15];           /* [0-11] direct, [12] 1x ind, [13] 2x, [14] 3x */
    uint32_t i_generation;          /* File version (NFS) */
    uint32_t i_file_acl;            /* Extended attribute block */
    uint32_t i_dir_acl;             /* Directory ACL / high size bits */
    uint32_t i_faddr;               /* Fragment address */
    uint8_t  i_osd2[12];            /* OS dependent */
} __attribute__((packed));

/**
 * Directory entry (variable length).  The name is NOT null-terminated;
 * entries are packed and rec_len (always a multiple of 4) chains them.
 * The trailing bytes between the end of the name and the next entry are
 * part of rec_len.
 *
 * Used when EXT2_FEATURE_INCOMPAT_FILETYPE is set (entries carry an extra
 * file_type byte).  Without that feature the layout is:
 *   u32 inode, u16 rec_len, u8 name_len, name[]  (i.e. 4 bytes shorter).
 */
struct ext2_dirent {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed));

struct ext2_dirent_legacy {
    uint32_t inode;
    uint16_t rec_len;
    uint16_t name_len;      /* legacy format: 16-bit name length */
    char     name[];
} __attribute__((packed));

/* ============================================================================
 * In-Memory Structures
 * ============================================================================ */

/**
 * ext2 filesystem information (per-mount)
 */
typedef struct ext2_fs_info {
    struct ext2_super_block sb;
    struct ext2_group_desc *gdt;        /* All group descriptors in memory */
    int device_id;                      /* Block device (prim id) */
    int partition_id;                   /* Partition (scnd id) */
    uint32_t block_size;                /* 1024, 2048 or 4096 */
    uint32_t inode_size;                /* 128 or 256 (on-disk stride) */
    uint32_t num_groups;
    uint32_t inodes_per_group;
    uint32_t blocks_per_group;
    uint32_t blocks_per_group_mask;     /* blocks_per_group - 1 (power of 2) */

    /* Cached block/inode bitmaps for one group (loaded on demand) */
    int      bitmap_group;              /* group whose bitmaps are loaded, -1 = none */
    uint8_t *block_bitmap;
    uint8_t *inode_bitmap;
    int      block_bitmap_dirty;
    int      inode_bitmap_dirty;
    int      gdt_dirty;                 /* group descriptors need write-back */
    int      sb_dirty;                  /* superblock free counts need write-back */

    /* set once we clear the dir_index feature so it can be written once */
    int      dir_index_pending;
} ext2_fs_info_t;

/**
 * ext2 file information (per-open-file)
 */
typedef struct ext2_file_info {
    ext2_fs_info_t *fs;
    uint32_t inode_num;
    struct ext2_inode inode;            /* Cached inode data */
    uint32_t offset;                    /* Current read/write position */
    uint32_t dir_pos;                   /* Byte offset within directory */
    int dirty;                          /* Inode needs write-back */
} ext2_file_info_t;

/* ============================================================================
 * Function Declarations
 * ============================================================================ */

/* super.c - Superblock operations */
int ext2_mount(int dev_id, int part_id, void **fs_private);
int ext2_unmount(void *fs_private);

/* super.c - raw block I/O */
int ext2_read_block(ext2_fs_info_t *fs, uint32_t block, void *buf);
int ext2_write_block(ext2_fs_info_t *fs, uint32_t block, const void *buf);

/* inode.c - Inode operations */
int ext2_read_inode(ext2_fs_info_t *fs, uint32_t ino, struct ext2_inode *inode);
int ext2_write_inode(ext2_fs_info_t *fs, uint32_t ino, struct ext2_inode *inode);
int ext2_bmap(ext2_fs_info_t *fs, struct ext2_inode *inode,
              uint32_t file_block, uint32_t *block_out);
int ext2_bmap_alloc(ext2_fs_info_t *fs, uint32_t ino, struct ext2_inode *inode,
                    uint32_t file_block, uint32_t *block_out);
int ext2_alloc_block(ext2_fs_info_t *fs, uint32_t ino, uint32_t *block_out);
int ext2_free_block(ext2_fs_info_t *fs, uint32_t block);
int ext2_alloc_inode(ext2_fs_info_t *fs, uint16_t mode, uint32_t pref_group,
                     uint32_t *ino_out);
int ext2_free_inode(ext2_fs_info_t *fs, uint32_t ino);
int ext2_sync_bitmaps(ext2_fs_info_t *fs);
int ext2_sync_super(ext2_fs_info_t *fs);
int ext2_truncate_blocks(ext2_fs_info_t *fs, struct ext2_inode *inode,
                         uint32_t new_blocks, uint32_t old_blocks);
void ext2_refresh_blocks(ext2_fs_info_t *fs, struct ext2_inode *inode);

int ext2_init_dir(ext2_fs_info_t *fs, struct ext2_inode *dir,
                  uint32_t dir_ino, uint32_t parent_ino);

/* dir.c - Directory operations */
int ext2_lookup(ext2_fs_info_t *fs, struct ext2_inode *dir_inode,
                const char *name, uint32_t *ino_out);
int ext2_add_dirent(ext2_fs_info_t *fs, uint32_t dir_ino,
                    struct ext2_inode *dir_inode,
                    const char *name, uint32_t ino, uint8_t ftype);
int ext2_remove_dirent(ext2_fs_info_t *fs, struct ext2_inode *dir_inode,
                       const char *name);

/* file.c - File operations */
int ext2_read_file(ext2_fs_info_t *fs, struct ext2_inode *inode,
                   uint32_t offset, uint8_t *buf, uint32_t count);
int ext2_write_file(ext2_fs_info_t *fs, uint32_t ino, struct ext2_inode *inode,
                    uint32_t offset, const uint8_t *buf, uint32_t count);
int ext2_truncate_file(ext2_fs_info_t *fs, uint32_t ino,
                       struct ext2_inode *inode, uint32_t new_size);
void ext2_fill_stat(const struct ext2_inode *inode, uint32_t ino, stat_t *st);

/* ext2.c - Initialization */
void ext2_init(void);

#endif /* EXT2_H */
