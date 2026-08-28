#include "driver/block/part_mbr.h"
#include "driver/driver.h"
#include "fs/fs.h"
#include "lib/printk.h"
#include "lib/string.h"
#include <stddef.h>
#include <stdint.h>

/* =========================================================================
 * Global Partition Table
 * 
 * Store partition info for all registered disks (max MBR_MAX_DISKS × 4)
 * ========================================================================= */

static partition_info_t partitions[MBR_MAX_DISKS][MBR_PARTITION_COUNT];

/* =========================================================================
 * Registered Disk Table
 *
 * Block drivers (IDE, AHCI) call mbr_register_disk() to make their raw
 * disks visible to the MBR scanner.  mbr_init() walks this table.
 * ========================================================================= */

typedef struct {
    bool         present;
    char         name[8];       /* "hda", "sda", ... */
    int          base_prim;     /* first prim_id of the disk family */
    int          disk_idx;      /* index within the family */
    disk_read_fn read;
    disk_write_fn write;
} mbr_disk_t;

static mbr_disk_t mbr_disks[MBR_MAX_DISKS];
static int mbr_disk_count = 0;

void mbr_register_disk(const char *name, int base_prim, int disk_idx,
                       disk_read_fn read, disk_write_fn write)
{
    mbr_disk_t *d;

    if (mbr_disk_count >= MBR_MAX_DISKS || !read || !write)
        return;

    d = &mbr_disks[mbr_disk_count];
    memset(d, 0, sizeof(*d));
    d->present = true;
    strncpy(d->name, name, sizeof(d->name) - 1);
    d->base_prim = base_prim;
    d->disk_idx = disk_idx;
    d->read = read;
    d->write = write;
    mbr_disk_count++;
}

/* =========================================================================
 * Helper Functions
 * ========================================================================= */

/**
 * Parse MBR from a disk and populate partition table
 * Returns number of valid partitions found
 */
static int mbr_parse_disk(int disk_index)
{
    uint8_t mbr_buffer[512];
    mbr_t *mbr = (mbr_t *)mbr_buffer;
    mbr_disk_t *d = &mbr_disks[disk_index];
    int partition_count = 0;
    
    /* Read MBR (sector 0) through the disk's registered read function */
    if (d->read(d->disk_idx, 0, 1, mbr_buffer) != 0) {
        printk("[MBR] Failed to read MBR from %s\n", d->name);
        return 0;
    }
    
    /* Verify MBR signature */
    if (mbr->signature != MBR_SIGNATURE) {
        printk("[MBR] Invalid MBR signature on %s (0x%04x)\n", 
               d->name, mbr->signature);
        return 0;
    }
    
    /* Parse partition entries */
    for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
        mbr_partition_entry_t *entry = &mbr->partitions[i];
        partition_info_t *part = &partitions[disk_index][i];
        
        /* Check if partition exists (non-zero type) */
        if (entry->type != PART_TYPE_EMPTY && entry->lba_count > 0) {
            part->valid = true;
            part->disk_id = disk_index;
            part->partition_num = i + 1;  /* 1-based partition numbering */
            part->type = entry->type;
            part->bootable = (entry->status == 0x80);
            part->lba_start = entry->lba_start;
            part->lba_count = entry->lba_count;
            partition_count++;
        } else {
            part->valid = false;
        }
    }
    
    return partition_count;
}

/**
 * Get partition type name for printing
 */
static const char *mbr_get_type_name(uint8_t type)
{
    switch (type) {
        case PART_TYPE_EMPTY:       return "Empty";
        case PART_TYPE_FAT16_LBA:   return "FAT16-LBA";
        case PART_TYPE_FAT32_LBA:   return "FAT32-LBA";
        case PART_TYPE_NTFS:        return "NTFS";
        case PART_TYPE_MINIX:       return "Minix";
        case PART_TYPE_LINUX:       return "Linux";
        case PART_TYPE_LINUX_SWAP:  return "Linux Swap";
        case PART_TYPE_EXTENDED:    return "Extended";
        default:                    return "Unknown";
    }
}

/* =========================================================================
 * Block Device Callbacks for Partitions
 * ========================================================================= */

/* Find the MBR disk table index whose partition block device has this
 * prim_id (partition prim_id = base_prim + 4 + disk_idx). */
static int mbr_find_partition_disk(int prim_id)
{
    for (int i = 0; i < mbr_disk_count; i++) {
        if (mbr_disks[i].base_prim + 4 + mbr_disks[i].disk_idx == prim_id)
            return i;
    }
    return -1;
}

/**
 * Partition block device read callback
 * prim_id: partition block device (hdaX=4..7, sdaX=12..15)
 * scnd_id: partition number (1-4)
 * offset: sector offset within partition (0-based)
 * count: number of blocks
 */
static int part_block_read(int prim_id, int scnd_id, void *buf, uint32_t offset, size_t count)
{
    int d_idx = mbr_find_partition_disk(prim_id);
    if (d_idx < 0)
        return -1;

    mbr_disk_t *d = &mbr_disks[d_idx];

    /* Validate partition number (1-4) */
    if (scnd_id < 1 || scnd_id > MBR_PARTITION_COUNT) {
        return -1;
    }
    
    /* Get partition info */
    partition_info_t *part = &partitions[d_idx][scnd_id - 1];
    
    /* Check if partition exists */
    if (!part->valid) {
        return -1;
    }
    
    /* Check bounds */
    if (offset >= part->lba_count || offset + count > part->lba_count) {
        return -1;
    }
    
    /* Calculate absolute LBA and read from the underlying raw disk */
    uint32_t absolute_lba = part->lba_start + offset;
    return bread(d->base_prim + d->disk_idx, 0, buf, absolute_lba, count);
}

/**
 * Partition block device write callback
 */
static int part_block_write(int prim_id, int scnd_id, const void *buf, uint32_t offset, size_t count)
{
    int d_idx = mbr_find_partition_disk(prim_id);
    if (d_idx < 0)
        return -1;

    mbr_disk_t *d = &mbr_disks[d_idx];

    /* Validate partition number (1-4) */
    if (scnd_id < 1 || scnd_id > MBR_PARTITION_COUNT) {
        return -1;
    }
    
    /* Get partition info */
    partition_info_t *part = &partitions[d_idx][scnd_id - 1];
    
    /* Check if partition exists */
    if (!part->valid) {
        return -1;
    }
    
    /* Check bounds */
    if (offset >= part->lba_count || offset + count > part->lba_count) {
        return -1;
    }
    
    /* Calculate absolute LBA and write to the underlying raw disk */
    uint32_t absolute_lba = part->lba_start + offset;
    return bwrite(d->base_prim + d->disk_idx, 0, buf, absolute_lba, count);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void mbr_init(void)
{
    printk("[MBR] Scanning partition tables...\n");
    
    block_ops_t part_ops = {
        .read = part_block_read,
        .write = part_block_write,
        .ioctl = NULL
    };
    
    int total_partitions = 0;
    
    /* Initialize partition table */
    for (int disk = 0; disk < MBR_MAX_DISKS; disk++) {
        for (int part = 0; part < MBR_PARTITION_COUNT; part++) {
            partitions[disk][part].valid = false;
        }
    }
    
    /* Scan every registered disk (IDE + AHCI) for partitions */
    for (int d_idx = 0; d_idx < mbr_disk_count; d_idx++) {
        mbr_disk_t *d = &mbr_disks[d_idx];
        
        /* Parse MBR */
        int part_count = mbr_parse_disk(d_idx);
        
        if (part_count > 0) {
            /* Register partition block device for this disk.
             * Partition prim_id = base_prim + 4 + disk_idx, so the
             * underlying raw disk is prim_id - 4 (part_block_read/write
             * rely on this mapping). */
            int prim_id = d->base_prim + 4 + d->disk_idx;
            
            if (register_block_device(prim_id, &part_ops) == 0) {
                printk("[MBR] Registered %sX (partitions) as block device %d\n",
                       d->name, prim_id);
            }

            /* Expose each valid partition as /dev/<disk>X (e.g. /dev/sda1,
             * /dev/hdb2) so mount(2) can resolve it by name. */
            for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
                if (partitions[d_idx][i].valid) {
                    extern int devfs_register_device(const char *, uint8_t, int, int);
                    char node[8];
                    int n = 0;
                    const char *dn = d->name;
                    while (dn[n] && n < 4) {
                        node[n] = dn[n];
                        n++;
                    }
                    node[n++] = (char)('0' + partitions[d_idx][i].partition_num);
                    node[n] = '\0';
                    devfs_register_device(node, DT_BLKDEV, prim_id,
                                          partitions[d_idx][i].partition_num);
                }
            }
            
            printk("[MBR] Found %d partition%s on %s\n",
                   part_count, part_count > 1 ? "s" : "", d->name);
            total_partitions += part_count;
        } else {
            printk("[MBR] No valid partitions on %s\n", d->name);
        }
    }
    
    printk("[MBR] Total: %d partition%s\n", 
           total_partitions, total_partitions != 1 ? "s" : "");
}

void mbr_print_partitions(void)
{
    printk("\n=== Partition Table ===\n");
    
    for (int d_idx = 0; d_idx < mbr_disk_count; d_idx++) {
        mbr_disk_t *d = &mbr_disks[d_idx];
        
        bool has_partitions = false;
        for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
            if (partitions[d_idx][i].valid) {
                has_partitions = true;
                break;
            }
        }
        
        if (!has_partitions) {
            continue;
        }
        
        printk("\n%s:\n", d->name);
        
        for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
            partition_info_t *part = &partitions[d_idx][i];
            
            if (part->valid) {
                uint32_t size_mb = (part->lba_count / 2048);  /* sectors * 512 / (1024*1024) */
                
                printk("  %s%d: %s%s, Start: %u, Size: %u MB (%u sectors)\n",
                       d->name,
                       part->partition_num,
                       part->bootable ? "[BOOT] " : "",
                       mbr_get_type_name(part->type),
                       part->lba_start,
                       size_mb,
                       part->lba_count);
            }
        }
    }
    
    printk("\n");
}

const partition_info_t *mbr_get_partition_info(int disk_id, int partition_num)
{
    /* Validate parameters (disk_id indexes the MBR disk table) */
    if (disk_id < 0 || disk_id >= mbr_disk_count) {
        return NULL;
    }
    
    if (partition_num < 1 || partition_num > MBR_PARTITION_COUNT) {
        return NULL;
    }
    
    partition_info_t *part = &partitions[disk_id][partition_num - 1];
    
    if (!part->valid) {
        return NULL;
    }
    
    return part;
}

int mbr_read_partition(uint8_t disk_id, uint8_t partition_num, 
                       uint32_t sector_offset, uint8_t count, void *buffer)
{
    /* Validate disk_id (index into the MBR disk table) */
    if (disk_id >= mbr_disk_count) {
        return -1;
    }
    
    /* Validate partition number (1-4) */
    if (partition_num < 1 || partition_num > MBR_PARTITION_COUNT) {
        return -1;
    }
    
    /* Get partition info */
    partition_info_t *part = &partitions[disk_id][partition_num - 1];
    
    /* Check if partition exists */
    if (!part->valid) {
        return -1;
    }
    
    /* CRITICAL: Check sector bounds to prevent reading beyond partition */
    if (sector_offset >= part->lba_count) {
        printk("[MBR] ERROR: sector_offset %u >= partition size %u\n",
               sector_offset, part->lba_count);
        return -1;
    }
    
    if (sector_offset + count > part->lba_count) {
        printk("[MBR] ERROR: read would exceed partition bounds "
               "(offset=%u, count=%u, partition_size=%u)\n",
               sector_offset, count, part->lba_count);
        return -1;
    }
    
    /* CRITICAL FIX: Calculate absolute LBA by adding partition start to offset
     * This was the bug - the old code forgot to add sector_offset! */
    uint32_t absolute_lba = part->lba_start + sector_offset;
    
    /* Read from the underlying disk through its registered read function */
    return mbr_disks[disk_id].read(mbr_disks[disk_id].disk_idx,
                                   absolute_lba, count, buffer);
}

int mbr_write_partition(uint8_t disk_id, uint8_t partition_num,
                        uint32_t sector_offset, uint8_t count, const void *buffer)
{
    /* Validate disk_id (index into the MBR disk table) */
    if (disk_id >= mbr_disk_count) {
        return -1;
    }
    
    /* Validate partition number (1-4) */
    if (partition_num < 1 || partition_num > MBR_PARTITION_COUNT) {
        return -1;
    }
    
    /* Get partition info */
    partition_info_t *part = &partitions[disk_id][partition_num - 1];
    
    /* Check if partition exists */
    if (!part->valid) {
        return -1;
    }
    
    /* CRITICAL: Check sector bounds to prevent writing beyond partition */
    if (sector_offset >= part->lba_count) {
        printk("[MBR] ERROR: sector_offset %u >= partition size %u\n",
               sector_offset, part->lba_count);
        return -1;
    }
    
    if (sector_offset + count > part->lba_count) {
        printk("[MBR] ERROR: write would exceed partition bounds "
               "(offset=%u, count=%u, partition_size=%u)\n",
               sector_offset, count, part->lba_count);
        return -1;
    }
    
    /* CRITICAL FIX: Calculate absolute LBA by adding partition start to offset */
    uint32_t absolute_lba = part->lba_start + sector_offset;
    
    /* Write to the underlying disk through its registered write function */
    return mbr_disks[disk_id].write(mbr_disks[disk_id].disk_idx,
                                    absolute_lba, count, buffer);
}