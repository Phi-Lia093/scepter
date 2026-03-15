#ifndef MINIX3_H
#define MINIX3_H

#include <stdint.h>

/* ============================================================================
 * Minix v3 Filesystem Structures
 * Based on actual disk analysis of Linux mkfs.minix -3 output
 * ============================================================================ */

/* Magic number */
#define MINIX3_SUPER_MAGIC   0x4d5a  /* "ZM" in little-endian */
#define MINIX3_SUPER_MAGIC2  0x4d5b  /* Alternative magic (v3 with 4KB blocks) */

/* Filesystem constants */
#define MINIX3_ROOT_INO      1       /* Root directory inode number */
#define MINIX3_BLOCK_SIZE    1024    /* Default block size (can be 4096) */
#define MINIX3_NAME_LEN      60      /* Maximum filename length */
#define MINIX3_INODE_SIZE    64      /* Size of inode structure */
#define MINIX3_DIRENT_SIZE   64      /* Size of directory entry */

/* Inode structure - 64 bytes */
#define MINIX3_DIRECT_ZONES  7       /* Number of direct zone pointers */
#define MINIX3_INDIR_ZONES   10      /* Total zone pointers (7+3) */

/* File type and permission bits */
#define MINIX3_S_IFMT    0xF000  /* File type mask */
#define MINIX3_S_IFREG   0x8000  /* Regular file */
#define MINIX3_S_IFDIR   0x4000  /* Directory */
#define MINIX3_S_IFCHR   0x2000  /* Character device */
#define MINIX3_S_IFBLK   0x6000  /* Block device */
#define MINIX3_S_IFIFO   0x1000  /* FIFO */
#define MINIX3_S_IFLNK   0xA000  /* Symbolic link */

#define MINIX3_S_ISUID   0x0800  /* Set UID */
#define MINIX3_S_ISGID   0x0400  /* Set GID */
#define MINIX3_S_ISVTX   0x0200  /* Sticky bit */

#define MINIX3_S_IRWXU   0x01C0  /* User RWX */
#define MINIX3_S_IRUSR   0x0100  /* User read */
#define MINIX3_S_IWUSR   0x0080  /* User write */
#define MINIX3_S_IXUSR   0x0040  /* User execute */

#define MINIX3_S_IRWXG   0x0038  /* Group RWX */
#define MINIX3_S_IRGRP   0x0020  /* Group read */
#define MINIX3_S_IWGRP   0x0010  /* Group write */
#define MINIX3_S_IXGRP   0x0008  /* Group execute */

#define MINIX3_S_IRWXO   0x0007  /* Other RWX */
#define MINIX3_S_IROTH   0x0004  /* Other read */
#define MINIX3_S_IWOTH   0x0002  /* Other write */
#define MINIX3_S_IXOTH   0x0001  /* Other execute */

/* Helper macros for file type checking */
#define MINIX3_ISREG(m)  (((m) & MINIX3_S_IFMT) == MINIX3_S_IFREG)
#define MINIX3_ISDIR(m)  (((m) & MINIX3_S_IFMT) == MINIX3_S_IFDIR)
#define MINIX3_ISCHR(m)  (((m) & MINIX3_S_IFMT) == MINIX3_S_IFCHR)
#define MINIX3_ISBLK(m)  (((m) & MINIX3_S_IFMT) == MINIX3_S_IFBLK)
#define MINIX3_ISFIFO(m) (((m) & MINIX3_S_IFMT) == MINIX3_S_IFIFO)
#define MINIX3_ISLNK(m)  (((m) & MINIX3_S_IFMT) == MINIX3_S_IFLNK)

/* ============================================================================
 * On-Disk Structures
 * ============================================================================ */

/**
 * Minix v3 Superblock - Located at block 1 (offset 1024)
 * Size: 64 bytes (rest of block is padding)
 */
struct minix3_super_block {
    uint32_t s_ninodes;        /* Number of inodes */
    uint16_t s_pad0;           /* Padding */
    uint16_t s_imap_blocks;    /* Inode bitmap blocks */
    uint16_t s_zmap_blocks;    /* Zone bitmap blocks */
    uint16_t s_firstdatazone;  /* First data zone */
    uint16_t s_log_zone_size;  /* log2(block_size/1024) */
    uint16_t s_pad1;           /* Padding */
    uint32_t s_max_size;       /* Maximum file size */
    uint32_t s_zones;          /* Number of zones */
    uint16_t s_magic;          /* Magic number (0x4d5a) */
    uint16_t s_pad2;           /* Padding */
    uint16_t s_blocksize;      /* Block size in bytes */
    uint8_t  s_disk_version;   /* Filesystem version */
} __attribute__((packed));

/**
 * Minix v3 Inode - 64 bytes
 * Inodes are 1-based (inode 0 is invalid, root is inode 1)
 */
struct minix3_inode {
    uint16_t i_mode;           /* File mode and type */
    uint16_t i_nlinks;         /* Number of hard links */
    uint16_t i_uid;            /* User ID */
    uint16_t i_gid;            /* Group ID */
    uint32_t i_size;           /* File size in bytes */
    uint32_t i_atime;          /* Access time (Unix timestamp) */
    uint32_t i_mtime;          /* Modification time */
    uint32_t i_ctime;          /* Change time */
    uint32_t i_zone[10];       /* Zone pointers:
                                 * [0-6]: Direct zones
                                 * [7]:   Single indirect
                                 * [8]:   Double indirect  
                                 * [9]:   Triple indirect (rarely used) */
} __attribute__((packed));

/**
 * Minix v3 Directory Entry - 64 bytes
 * inode=0 means the entry is unused/deleted
 */
struct minix3_dirent {
    uint32_t inode;            /* Inode number (0 = unused) */
    char     name[60];         /* Filename (null-terminated) */
} __attribute__((packed));

/* ============================================================================
 * In-Memory Structures
 * ============================================================================ */

/**
 * Minix v3 filesystem information (per-mount)
 */
typedef struct minix3_fs_info {
    /* Superblock data */
    struct minix3_super_block sb;
    
    /* Device info */
    int device_id;             /* IDE device ID (0-3) */
    int partition_id;          /* Partition ID (1-4) */
    
    /* Calculated values */
    uint32_t block_size;       /* Block size (1024 or 4096) */
    uint32_t inode_table_block; /* First block of inode table */
    uint32_t data_zone_start;  /* First data zone number */
    
    /* Bitmaps (loaded into memory) */
    uint8_t *inode_bitmap;     /* Inode allocation bitmap */
    uint8_t *zone_bitmap;      /* Zone allocation bitmap */
    int imap_dirty;            /* Inode bitmap needs sync */
    int zmap_dirty;            /* Zone bitmap needs sync */
} minix3_fs_info_t;

/**
 * Minix v3 file information (per-open-file)
 */
typedef struct minix3_file_info {
    minix3_fs_info_t *fs;      /* Filesystem this file belongs to */
    uint32_t inode_num;        /* Inode number */
    struct minix3_inode inode; /* Cached inode data */
    uint32_t offset;           /* Current read/write position */
    uint32_t dir_pos;          /* Current directory entry (for readdir) */
    int dirty;                 /* Inode needs to be written back */
} minix3_file_info_t;

/* ============================================================================
 * Function Declarations
 * ============================================================================ */

/* super.c - Superblock operations */
int minix3_mount(int dev_id, int part_id, void **fs_private);
int minix3_unmount(void *fs_private);
minix3_fs_info_t *minix3_get_mounted_fs(void);  /* For testing */

/* inode.c - Inode operations */
int minix3_read_inode(minix3_fs_info_t *fs, uint32_t ino, struct minix3_inode *inode);
int minix3_bmap(minix3_fs_info_t *fs, struct minix3_inode *inode, 
                uint32_t file_block, uint32_t *zone_out);

/* dir.c - Directory operations */
int minix3_lookup(minix3_fs_info_t *fs, struct minix3_inode *dir_inode,
                  const char *name, uint32_t *inode_out);
int minix3_list_dir(minix3_fs_info_t *fs, struct minix3_inode *dir_inode);

/* file.c - File operations */
int minix3_read_file(minix3_fs_info_t *fs, struct minix3_inode *inode,
                     uint32_t offset, uint8_t *buf, uint32_t count);
int minix3_read_entire_file(minix3_fs_info_t *fs, struct minix3_inode *inode,
                            uint8_t *buf, uint32_t max_size);
void minix3_cat_file(minix3_fs_info_t *fs, struct minix3_inode *inode, uint32_t max_bytes);

/* minix3.c - Initialization */
void minix3_init(void);

#endif /* MINIX3_H */
