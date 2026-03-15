/* ============================================================================
 * Minix v3 Filesystem - Superblock Operations
 * ============================================================================ */

#include "fs/minix3.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "lib/string.h"

/* Global pointer to mounted filesystem (for testing) */
static minix3_fs_info_t *g_mounted_fs = NULL;

/**
 * Mount a Minix v3 filesystem
 * @param dev_id Device ID (0-3 for IDE devices, 4-7 for partitions)
 * @param part_id Partition ID (1-4, or 0 for whole disk)
 * @param fs_private Output: pointer to allocated fs_info structure
 * @return 0 on success, -1 on error
 */
int minix3_mount(int dev_id, int part_id, void **fs_private)
{
    printk("[minix3] Mounting device %d, partition %d\n", dev_id, part_id);
    
    /* Allocate filesystem info structure */
    minix3_fs_info_t *fs = (minix3_fs_info_t *)kalloc(sizeof(minix3_fs_info_t));
    if (!fs) {
        printk("[minix3] Failed to allocate fs_info\n");
        return -1;
    }
    memset(fs, 0, sizeof(minix3_fs_info_t));
    
    fs->device_id = dev_id;
    fs->partition_id = part_id;
    
    /* Read superblock (block 1 = 2 sectors at offset 1024 from partition start)
     * Note: bread() reads from partition start automatically */
    uint8_t sb_buf[MINIX3_BLOCK_SIZE];
    int ret = bread(dev_id, part_id, sb_buf, 2, 2);  /* Sectors 2-3 = block 1 */
    if (ret < 0) {
        printk("[minix3] Failed to read superblock\n");
        kfree(fs);
        return -1;
    }
    
    /* Parse superblock */
    struct minix3_super_block *sb = (struct minix3_super_block *)sb_buf;
    fs->sb = *sb;  /* Copy superblock data */
    
    /* Validate magic number */
    if (sb->s_magic != MINIX3_SUPER_MAGIC && sb->s_magic != MINIX3_SUPER_MAGIC2) {
        printk("[minix3] Invalid magic number: 0x%04x (expected 0x%04x)\n",
               sb->s_magic, MINIX3_SUPER_MAGIC);
        kfree(fs);
        return -1;
    }
    
    /* Calculate block size */
    fs->block_size = sb->s_blocksize;
    if (fs->block_size != 1024 && fs->block_size != 4096) {
        printk("[minix3] Unsupported block size: %u\n", fs->block_size);
        kfree(fs);
        return -1;
    }
    
    /* Calculate inode table location */
    fs->inode_table_block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks;
    fs->data_zone_start = sb->s_firstdatazone;
    
    printk("[minix3] Mounted: %u inodes, %u blocks\n", 
           sb->s_ninodes, sb->s_zones);
    
    /* Allocate and load inode bitmap */
    uint32_t imap_size = sb->s_imap_blocks * fs->block_size;
    fs->inode_bitmap = (uint8_t *)kalloc(imap_size);
    if (!fs->inode_bitmap) {
        printk("[minix3] Failed to allocate inode bitmap\n");
        kfree(fs);
        return -1;
    }
    
    /* Read inode bitmap (starts at block 2) */
    for (uint32_t i = 0; i < sb->s_imap_blocks; i++) {
        uint32_t block_num = 2 + i;
        uint32_t sector = block_num * (fs->block_size / 512);
        int sectors_per_block = fs->block_size / 512;
        
        ret = bread(dev_id, part_id, 
                   fs->inode_bitmap + (i * fs->block_size),
                   sector, sectors_per_block);
        if (ret < 0) {
            printk("[minix3] Failed to read inode bitmap block %u\n", i);
            kfree(fs->inode_bitmap);
            kfree(fs);
            return -1;
        }
    }
    
    /* Allocate and load zone bitmap */
    uint32_t zmap_size = sb->s_zmap_blocks * fs->block_size;
    fs->zone_bitmap = (uint8_t *)kalloc(zmap_size);
    if (!fs->zone_bitmap) {
        printk("[minix3] Failed to allocate zone bitmap\n");
        kfree(fs->inode_bitmap);
        kfree(fs);
        return -1;
    }
    
    /* Read zone bitmap (starts after inode bitmap) */
    uint32_t zmap_start_block = 2 + sb->s_imap_blocks;
    for (uint32_t i = 0; i < sb->s_zmap_blocks; i++) {
        uint32_t block_num = zmap_start_block + i;
        uint32_t sector = block_num * (fs->block_size / 512);
        int sectors_per_block = fs->block_size / 512;
        
        ret = bread(dev_id, part_id,
                   fs->zone_bitmap + (i * fs->block_size),
                   sector, sectors_per_block);
        if (ret < 0) {
            printk("[minix3] Failed to read zone bitmap block %u\n", i);
            kfree(fs->zone_bitmap);
            kfree(fs->inode_bitmap);
            kfree(fs);
            return -1;
        }
    }
    
    *fs_private = fs;
    g_mounted_fs = fs;  /* Save for testing */
    printk("[minix3] Mount successful\n");
    return 0;
}

/**
 * Get the mounted filesystem (for testing)
 */
minix3_fs_info_t *minix3_get_mounted_fs(void)
{
    return g_mounted_fs;
}

/**
 * Unmount a Minix v3 filesystem
 */
int minix3_unmount(void *fs_private)
{
    if (!fs_private) return -1;
    
    minix3_fs_info_t *fs = (minix3_fs_info_t *)fs_private;
    
    /* Free bitmaps */
    if (fs->inode_bitmap) kfree(fs->inode_bitmap);
    if (fs->zone_bitmap) kfree(fs->zone_bitmap);
    
    /* Free fs structure */
    kfree(fs);
    
    printk("[minix3] Unmounted\n");
    return 0;
}