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