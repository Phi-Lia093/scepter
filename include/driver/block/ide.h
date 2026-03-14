#ifndef IDE_H
#define IDE_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * IDE/ATA Controller Definitions
 * ========================================================================= */

/* IDE Controller I/O Ports */
#define IDE_PRIMARY_BASE    0x1F0
#define IDE_PRIMARY_CTRL    0x3F6
#define IDE_SECONDARY_BASE  0x170
#define IDE_SECONDARY_CTRL  0x376

/* IDE Register Offsets (from base port) */
#define IDE_REG_DATA        0x00    /* Data register (16-bit) */
#define IDE_REG_ERROR       0x01    /* Error register (read) */
#define IDE_REG_FEATURES    0x01    /* Features register (write) */
#define IDE_REG_SECCOUNT    0x02    /* Sector count */
#define IDE_REG_LBA_LOW     0x03    /* LBA low byte */
#define IDE_REG_LBA_MID     0x04    /* LBA mid byte */
#define IDE_REG_LBA_HIGH    0x05    /* LBA high byte */
#define IDE_REG_DRIVE       0x06    /* Drive/Head register */
#define IDE_REG_STATUS      0x07    /* Status register (read) */
#define IDE_REG_COMMAND     0x07    /* Command register (write) */

/* IDE Control Register */
#define IDE_REG_CONTROL     0x00    /* Offset from control port */
#define IDE_REG_ALTSTATUS   0x00    /* Alternate status (read from control) */

/* IDE Status Register Bits */
#define IDE_STATUS_ERR      0x01    /* Error */
#define IDE_STATUS_IDX      0x02    /* Index */
#define IDE_STATUS_CORR     0x04    /* Corrected data */
#define IDE_STATUS_DRQ      0x08    /* Data request */
#define IDE_STATUS_DSC      0x10    /* Drive seek complete */
#define IDE_STATUS_DF       0x20    /* Drive fault */
#define IDE_STATUS_DRDY     0x40    /* Drive ready */
#define IDE_STATUS_BSY      0x80    /* Busy */

/* IDE Commands */
#define IDE_CMD_READ_PIO        0x20    /* Read sectors with retry */
#define IDE_CMD_WRITE_PIO       0x30    /* Write sectors with retry */
#define IDE_CMD_IDENTIFY        0xEC    /* Identify device */

/* IDE Drive Selection Bits */
#define IDE_DRIVE_MASTER    0xA0    /* Select master drive (LBA mode) */
#define IDE_DRIVE_SLAVE     0xB0    /* Select slave drive (LBA mode) */

/* Maximum values */
#define IDE_MAX_DISKS       4       /* Max 4 disks (2 buses × 2 drives) */
#define IDE_SECTOR_SIZE     512     /* Standard sector size */

/* =========================================================================
 * IDE Disk Structure
 * ========================================================================= */

typedef struct {
    bool     exists;                /* True if disk is present */
    uint16_t base_port;             /* Base I/O port (0x1F0 or 0x170) */
    uint16_t ctrl_port;             /* Control I/O port (0x3F6 or 0x376) */
    uint8_t  drive;                 /* 0=master, 1=slave */
    uint32_t sectors;               /* Total number of sectors (LBA28) */
    char     model[41];             /* Model string (40 chars + null) */
} ide_disk_t;

/* =========================================================================
 * Global IDE Disk Array
 * ========================================================================= */

extern ide_disk_t ide_disks[IDE_MAX_DISKS];

/* =========================================================================
 * IDE Function Prototypes
 * ========================================================================= */

/**
 * Initialize IDE controller and scan for disks
 * Detects up to 4 disks (primary master/slave, secondary master/slave)
 */
void ide_init(void);

/**
 * Read sectors from an IDE disk using LBA28 addressing
 * 
 * @param disk_id Disk ID (0-3)
 * @param lba Starting LBA sector number
 * @param count Number of sectors to read (1-256)
 * @param buffer Buffer to store read data (must be count * 512 bytes)
 * @return 0 on success, -1 on error
 */
int ide_read_sectors(uint8_t disk_id, uint32_t lba, uint8_t count, void *buffer);

/**
 * Write sectors to an IDE disk using LBA28 addressing
 * 
 * @param disk_id Disk ID (0-3)
 * @param lba Starting LBA sector number
 * @param count Number of sectors to write (1-256)
 * @param buffer Buffer containing data to write (must be count * 512 bytes)
 * @return 0 on success, -1 on error
 */
int ide_write_sectors(uint8_t disk_id, uint32_t lba, uint8_t count, const void *buffer);

/**
 * Print information about detected IDE disks
 */
void ide_print_disks(void);

#endif /* IDE_H */
