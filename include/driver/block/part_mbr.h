#ifndef PART_MBR_H
#define PART_MBR_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * MBR (Master Boot Record) Partition Table Support
 *
 * Provides direct partition access functions (not through driver layer).
 * The driver abstraction layer doesn't fit the partition use case well,
 * so we provide dedicated partition I/O functions instead.
 * ========================================================================= */

/* MBR Constants */
#define MBR_SIGNATURE           0xAA55
#define MBR_PARTITION_COUNT     4
#define MBR_BOOTSTRAP_SIZE      446

/* Maximum disks tracked (4 IDE + 4 AHCI). */
#define MBR_MAX_DISKS           8

/* Partition Types (common ones) */
#define PART_TYPE_EMPTY         0x00
#define PART_TYPE_FAT16_LBA     0x0E
#define PART_TYPE_NTFS          0x07
#define PART_TYPE_FAT32_LBA     0x0C
#define PART_TYPE_MINIX         0x81
#define PART_TYPE_LINUX         0x83
#define PART_TYPE_LINUX_SWAP    0x82
#define PART_TYPE_EXTENDED      0x05

/* =========================================================================
 * Disk registration
 *
 * Block drivers (IDE, AHCI) register every raw disk with mbr_register_disk()
 * so the MBR scanner can parse partitions on any of them.  Partition block
 * devices are numbered base_prim + 4 + disk_idx (e.g. IDE partitions 4-7,
 * AHCI partitions 12-15); the underlying raw disk is base_prim + disk_idx.
 * ========================================================================= */

/* Sector-level I/O callbacks implemented by the disk driver. */
typedef int (*disk_read_fn)(int disk_idx, uint32_t lba, uint8_t count,
                            void *buffer);
typedef int (*disk_write_fn)(int disk_idx, uint32_t lba, uint8_t count,
                             const void *buffer);

/**
 * Register a raw disk for MBR scanning.
 * @param name       Device name, e.g. "hda" or "sda" (no partitions suffix)
 * @param base_prim  First prim_id of the disk family (0 = IDE, 8 = AHCI)
 * @param disk_idx   Index within the family (0-3)
 * @param read       Sector read function
 * @param write      Sector write function
 */
void mbr_register_disk(const char *name, int base_prim, int disk_idx,
                       disk_read_fn read, disk_write_fn write);

/* =========================================================================
 * MBR Structures
 * ========================================================================= */

/**
 * MBR Partition Entry (16 bytes)
 */
typedef struct {
    uint8_t  status;            /* 0x80 = bootable, 0x00 = not bootable */
    uint8_t  first_chs[3];      /* CHS of first sector (legacy, unused) */
    uint8_t  type;              /* Partition type */
    uint8_t  last_chs[3];       /* CHS of last sector (legacy, unused) */
    uint32_t lba_start;         /* LBA start sector */
    uint32_t lba_count;         /* Number of sectors in partition */
} __attribute__((packed)) mbr_partition_entry_t;

/**
 * Master Boot Record (512 bytes)
 */
typedef struct {
    uint8_t bootstrap[MBR_BOOTSTRAP_SIZE];     /* Boot code */
    mbr_partition_entry_t partitions[MBR_PARTITION_COUNT];  /* 4 partition entries */
    uint16_t signature;                         /* 0xAA55 magic number */
} __attribute__((packed)) mbr_t;

/**
 * Internal partition information
 */
typedef struct {
    bool     valid;             /* True if partition exists */
    uint8_t  disk_id;           /* Underlying disk ID (0-3) */
    uint8_t  partition_num;     /* Partition number (1-4) */
    uint8_t  type;              /* Partition type */
    bool     bootable;          /* True if bootable */
    uint32_t lba_start;         /* Start LBA sector */
    uint32_t lba_count;         /* Number of sectors */
} partition_info_t;

/* =========================================================================
 * Function Prototypes
 * ========================================================================= */

/**
 * Initialize MBR partition support
 * Scans all IDE disks for MBR partition tables
 */
void mbr_init(void);

/**
 * Print all detected partitions to console
 */
void mbr_print_partitions(void);

/**
 * Get partition information
 * @param disk_id Disk ID (0-3 for hda-hdd)
 * @param partition_num Partition number (1-4)
 * @return Partition info or NULL if invalid
 */
const partition_info_t *mbr_get_partition_info(int disk_id, int partition_num);

/**
 * Read sector(s) from a partition
 * @param disk_id Disk ID (0-3 for hda-hdd)
 * @param partition_num Partition number (1-4)
 * @param sector_offset Sector offset within partition (0-based)
 * @param count Number of sectors to read
 * @param buffer Buffer to read into (must be count * 512 bytes)
 * @return 0 on success, -1 on error
 */
int mbr_read_partition(uint8_t disk_id, uint8_t partition_num, 
                       uint32_t sector_offset, uint8_t count, void *buffer);

/**
 * Write sector(s) to a partition
 * @param disk_id Disk ID (0-3 for hda-hdd)
 * @param partition_num Partition number (1-4)
 * @param sector_offset Sector offset within partition (0-based)
 * @param count Number of sectors to write
 * @param buffer Buffer to write from (must be count * 512 bytes)
 * @return 0 on success, -1 on error
 */
int mbr_write_partition(uint8_t disk_id, uint8_t partition_num,
                        uint32_t sector_offset, uint8_t count, const void *buffer);

#endif /* PART_MBR_H */