/* ============================================================================
 * Minix v3 Filesystem - Inode Operations
 * ============================================================================ */

#include "fs/minix3.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "lib/printk.h"
#include "lib/string.h"

/**
 * Read an inode from disk
 * @param fs Filesystem info
 * @param ino Inode number (1-based)
 * @param inode Output buffer for inode
 * @return 0 on success, -1 on error
 */
int minix3_read_inode(minix3_fs_info_t *fs, uint32_t ino, struct minix3_inode *inode)
{
    if (ino == 0 || ino > fs->sb.s_ninodes) {
        printk("[minix3] Invalid inode number: %u\n", ino);
        return -1;
    }
    
    /* Calculate inode offset
     * Inode table starts at block: 2 + imap_blocks + zmap_blocks
     * Inodes are 1-based, so subtract 1 */
    uint32_t inode_index = ino - 1;
    uint32_t inode_block = fs->inode_table_block + (inode_index * MINIX3_INODE_SIZE) / fs->block_size;
    uint32_t offset_in_block = (inode_index * MINIX3_INODE_SIZE) % fs->block_size;
    
    /* Read the block containing the inode */
    uint8_t buf[4096];  /* Support up to 4KB blocks */
    uint32_t sector = inode_block * (fs->block_size / 512);
    int sectors_per_block = fs->block_size / 512;
    
    int ret = bread(fs->device_id, fs->partition_id, buf, sector, sectors_per_block);
    if (ret < 0) {
        printk("[minix3] Failed to read inode %u\n", ino);
        return -1;
    }
    
    /* Copy inode data */
    memcpy(inode, buf + offset_in_block, MINIX3_INODE_SIZE);
    return 0;
}

/**
 * Map file block to zone number (block mapping)
 * @param fs Filesystem info
 * @param inode Inode structure
 * @param file_block Block number within file (0-based)
 * @param zone_out Output: zone number (0 = sparse/hole)
 * @return 0 on success, -1 on error
 */
int minix3_bmap(minix3_fs_info_t *fs, struct minix3_inode *inode, 
                uint32_t file_block, uint32_t *zone_out)
{
    uint32_t zone_ptrs_per_block = fs->block_size / 4;  /* 4 bytes per zone pointer */
    
    /* Direct blocks (0-6) */
    if (file_block < MINIX3_DIRECT_ZONES) {
        *zone_out = inode->i_zone[file_block];
        return 0;
    }
    
    file_block -= MINIX3_DIRECT_ZONES;
    
    /* Single indirect (block 7) */
    if (file_block < zone_ptrs_per_block) {
        if (inode->i_zone[7] == 0) {
            *zone_out = 0;  /* Sparse */
            return 0;
        }
        
        /* Read indirect block */
        uint8_t ind_buf[4096];
        uint32_t sector = inode->i_zone[7] * (fs->block_size / 512);
        int sectors = fs->block_size / 512;
        
        if (bread(fs->device_id, fs->partition_id, ind_buf, sector, sectors) < 0) {
            printk("[minix3] Failed to read indirect block\n");
            return -1;
        }
        
        uint32_t *zones = (uint32_t *)ind_buf;
        *zone_out = zones[file_block];
        return 0;
    }
    
    file_block -= zone_ptrs_per_block;
    
    /* Double indirect (block 8) */
    if (file_block < zone_ptrs_per_block * zone_ptrs_per_block) {
        if (inode->i_zone[8] == 0) {
            *zone_out = 0;  /* Sparse */
            return 0;
        }
        
        uint32_t ind1_index = file_block / zone_ptrs_per_block;
        uint32_t ind2_index = file_block % zone_ptrs_per_block;
        
        /* Read first level indirect block */
        uint8_t ind1_buf[4096];
        uint32_t sector = inode->i_zone[8] * (fs->block_size / 512);
        int sectors = fs->block_size / 512;
        
        if (bread(fs->device_id, fs->partition_id, ind1_buf, sector, sectors) < 0) {
            printk("[minix3] Failed to read double indirect block (level 1)\n");
            return -1;
        }
        
        uint32_t *zones1 = (uint32_t *)ind1_buf;
        uint32_t ind2_zone = zones1[ind1_index];
        
        if (ind2_zone == 0) {
            *zone_out = 0;  /* Sparse */
            return 0;
        }
        
        /* Read second level indirect block */
        uint8_t ind2_buf[4096];
        sector = ind2_zone * (fs->block_size / 512);
        
        if (bread(fs->device_id, fs->partition_id, ind2_buf, sector, sectors) < 0) {
            printk("[minix3] Failed to read double indirect block (level 2)\n");
            return -1;
        }
        
        uint32_t *zones2 = (uint32_t *)ind2_buf;
        *zone_out = zones2[ind2_index];
        return 0;
    }
    
    /* Triple indirect not commonly used, return error */
    printk("[minix3] Triple indirect not supported (file too large)\n");
    return -1;
}

/**
 * Write an inode back to disk
 * @param fs Filesystem info
 * @param ino Inode number (1-based)
 * @param inode Inode structure to write
 * @return 0 on success, -1 on error
 */
int minix3_write_inode(minix3_fs_info_t *fs, uint32_t ino, struct minix3_inode *inode)
{
    if (ino == 0 || ino > fs->sb.s_ninodes) {
        printk("[minix3] Invalid inode number: %u\n", ino);
        return -1;
    }
    
    /* Calculate inode offset */
    uint32_t inode_index = ino - 1;
    uint32_t inode_block = fs->inode_table_block + (inode_index * MINIX3_INODE_SIZE) / fs->block_size;
    uint32_t offset_in_block = (inode_index * MINIX3_INODE_SIZE) % fs->block_size;
    
    /* Read the block containing the inode */
    uint8_t buf[4096];
    uint32_t sector = inode_block * (fs->block_size / 512);
    int sectors_per_block = fs->block_size / 512;
    
    if (bread(fs->device_id, fs->partition_id, buf, sector, sectors_per_block) < 0) {
        printk("[minix3] Failed to read inode block for write\n");
        return -1;
    }
    
    /* Update inode in buffer */
    memcpy(buf + offset_in_block, inode, MINIX3_INODE_SIZE);
    
    /* Write back */
    if (bwrite(fs->device_id, fs->partition_id, buf, sector, sectors_per_block) < 0) {
        printk("[minix3] Failed to write inode %u\n", ino);
        return -1;
    }
    
    return 0;
}

/**
 * Allocate a new zone (block)
 * @param fs Filesystem info
 * @param zone_out Output: allocated zone number
 * @return 0 on success, -1 on error
 */
int minix3_alloc_zone(minix3_fs_info_t *fs, uint32_t *zone_out)
{
    uint32_t total_zones = fs->sb.s_zones;
    uint32_t bitmap_bytes = fs->sb.s_zmap_blocks * fs->block_size;
    
    /* Search for a free zone in the bitmap */
    for (uint32_t byte_idx = 0; byte_idx < bitmap_bytes; byte_idx++) {
        if (fs->zone_bitmap[byte_idx] != 0xFF) {
            /* Found a byte with at least one free bit */
            for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
                if (!(fs->zone_bitmap[byte_idx] & (1 << bit_idx))) {
                    uint32_t zone = byte_idx * 8 + bit_idx;
                    
                    /* Check if zone is within valid range */
                    if (zone >= total_zones) {
                        return -1;  /* No more zones */
                    }
                    
                    /* Mark zone as used */
                    fs->zone_bitmap[byte_idx] |= (1 << bit_idx);
                    fs->zmap_dirty = 1;
                    
                    /* Return actual disk zone number (bitmap index + data zone start) */
                    *zone_out = zone + fs->data_zone_start;
                    return 0;
                }
            }
        }
    }
    
    printk("[minix3] No free zones available\n");
    return -1;
}

/**
 * Free a zone (block)
 * @param fs Filesystem info
 * @param zone Disk zone number to free
 * @return 0 on success, -1 on error
 */
int minix3_free_zone(minix3_fs_info_t *fs, uint32_t zone)
{
    if (zone < fs->data_zone_start || zone >= fs->sb.s_zones) {
        printk("[minix3] Invalid zone number: %u\n", zone);
        return -1;
    }
    
    /* Convert disk zone to bitmap index */
    uint32_t bitmap_zone = zone - fs->data_zone_start;
    uint32_t byte_idx = bitmap_zone / 8;
    int bit_idx = bitmap_zone % 8;
    
    /* Clear the bit */
    fs->zone_bitmap[byte_idx] &= ~(1 << bit_idx);
    fs->zmap_dirty = 1;
    
    return 0;
}

/**
 * Allocate a new inode
 * @param fs Filesystem info
 * @param mode File mode (type and permissions)
 * @param ino_out Output: allocated inode number
 * @return 0 on success, -1 on error
 */
int minix3_alloc_inode(minix3_fs_info_t *fs, uint16_t mode, uint32_t *ino_out)
{
    uint32_t total_inodes = fs->sb.s_ninodes;
    uint32_t bitmap_bytes = fs->sb.s_imap_blocks * fs->block_size;
    
    /* Search for a free inode in the bitmap (skip inode 0, start at 1) */
    for (uint32_t byte_idx = 0; byte_idx < bitmap_bytes; byte_idx++) {
        if (fs->inode_bitmap[byte_idx] != 0xFF) {
            /* Found a byte with at least one free bit */
            for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
                if (!(fs->inode_bitmap[byte_idx] & (1 << bit_idx))) {
                    uint32_t ino = byte_idx * 8 + bit_idx;
                    
                    /* Skip inode 0 (invalid) */
                    if (ino == 0) continue;
                    
                    /* Check if inode is within valid range */
                    if (ino > total_inodes) {
                        return -1;  /* No more inodes */
                    }
                    
                    /* Mark inode as used */
                    fs->inode_bitmap[byte_idx] |= (1 << bit_idx);
                    fs->imap_dirty = 1;
                    
                    /* Initialize the inode */
                    struct minix3_inode new_inode;
                    memset(&new_inode, 0, sizeof(new_inode));
                    new_inode.i_mode = mode;
                    new_inode.i_nlinks = 1;
                    new_inode.i_uid = 0;
                    new_inode.i_gid = 0;
                    new_inode.i_size = 0;
                    new_inode.i_atime = 1;  /* Non-zero timestamp (TODO: implement RTC) */
                    new_inode.i_mtime = 1;
                    new_inode.i_ctime = 1;
                    
                    /* Write inode to disk */
                    if (minix3_write_inode(fs, ino, &new_inode) < 0) {
                        /* Failed to write, unmark the bit */
                        fs->inode_bitmap[byte_idx] &= ~(1 << bit_idx);
                        fs->imap_dirty = 1;
                        return -1;
                    }
                    
                    *ino_out = ino;
                    return 0;
                }
            }
        }
    }
    
    printk("[minix3] No free inodes available\n");
    return -1;
}

/**
 * Free an inode
 * @param fs Filesystem info
 * @param ino Inode number to free
 * @return 0 on success, -1 on error
 */
int minix3_free_inode(minix3_fs_info_t *fs, uint32_t ino)
{
    if (ino == 0 || ino > fs->sb.s_ninodes) {
        printk("[minix3] Invalid inode number: %u\n", ino);
        return -1;
    }
    
    uint32_t byte_idx = ino / 8;
    int bit_idx = ino % 8;
    
    /* Clear the bit */
    fs->inode_bitmap[byte_idx] &= ~(1 << bit_idx);
    fs->imap_dirty = 1;
    
    return 0;
}

/**
 * Flush dirty bitmaps to disk
 * @param fs Filesystem info
 * @return 0 on success, -1 on error
 */
int minix3_sync_bitmaps(minix3_fs_info_t *fs)
{
    /* Write zone bitmap if dirty */
    if (fs->zmap_dirty) {
        uint32_t zmap_start_block = 2 + fs->sb.s_imap_blocks;
        for (uint32_t i = 0; i < fs->sb.s_zmap_blocks; i++) {
            uint32_t block_num = zmap_start_block + i;
            uint32_t sector = block_num * (fs->block_size / 512);
            int sectors_per_block = fs->block_size / 512;
            
            if (bwrite(fs->device_id, fs->partition_id,
                      fs->zone_bitmap + (i * fs->block_size),
                      sector, sectors_per_block) < 0) {
                printk("[minix3] Failed to sync zone bitmap block %u\n", i);
                return -1;
            }
        }
        fs->zmap_dirty = 0;
    }
    
    /* Write inode bitmap if dirty */
    if (fs->imap_dirty) {
        for (uint32_t i = 0; i < fs->sb.s_imap_blocks; i++) {
            uint32_t block_num = 2 + i;
            uint32_t sector = block_num * (fs->block_size / 512);
            int sectors_per_block = fs->block_size / 512;
            
            if (bwrite(fs->device_id, fs->partition_id,
                      fs->inode_bitmap + (i * fs->block_size),
                      sector, sectors_per_block) < 0) {
                printk("[minix3] Failed to sync inode bitmap block %u\n", i);
                return -1;
            }
        }
        fs->imap_dirty = 0;
    }
    
    return 0;
}
