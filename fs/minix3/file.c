/* ============================================================================
 * Minix v3 Filesystem - File Operations
 * ============================================================================ */

#include "fs/minix3.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "lib/printk.h"
#include "lib/string.h"

/**
 * Read data from a file
 * @param fs Filesystem info
 * @param inode File inode
 * @param offset Offset within file to start reading
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
int minix3_read_file(minix3_fs_info_t *fs, struct minix3_inode *inode,
                     uint32_t offset, uint8_t *buf, uint32_t count)
{
    if (!MINIX3_ISREG(inode->i_mode)) {
        printk("[minix3] Not a regular file\n");
        return -1;
    }
    
    /* Don't read past end of file */
    if (offset >= inode->i_size) {
        return 0;  /* EOF */
    }
    
    if (offset + count > inode->i_size) {
        count = inode->i_size - offset;
    }
    
    uint32_t bytes_read = 0;
    uint8_t block_buf[4096];  /* Support up to 4KB blocks */
    
    while (bytes_read < count) {
        /* Calculate which block and offset within block */
        uint32_t file_block = (offset + bytes_read) / fs->block_size;
        uint32_t block_offset = (offset + bytes_read) % fs->block_size;
        uint32_t bytes_in_block = fs->block_size - block_offset;
        
        if (bytes_in_block > count - bytes_read) {
            bytes_in_block = count - bytes_read;
        }
        
        /* Map file block to zone */
        uint32_t zone;
        if (minix3_bmap(fs, inode, file_block, &zone) < 0) {
            return -1;
        }
        
        /* Handle sparse blocks (holes in file) */
        if (zone == 0) {
            /* Sparse block - return zeros */
            memset(buf + bytes_read, 0, bytes_in_block);
            bytes_read += bytes_in_block;
            continue;
        }
        
        /* Read the block */
        uint32_t sector = zone * (fs->block_size / 512);
        int sectors = fs->block_size / 512;
        
        if (bread(fs->device_id, fs->partition_id, block_buf, sector, sectors) < 0) {
            printk("[minix3] Failed to read block %u (zone %u)\n", file_block, zone);
            return -1;
        }
        
        /* Copy data from block to output buffer */
        memcpy(buf + bytes_read, block_buf + block_offset, bytes_in_block);
        bytes_read += bytes_in_block;
    }
    
    return bytes_read;
}

/**
 * Read entire file contents (for testing)
 * @param fs Filesystem info
 * @param inode File inode
 * @param buf Buffer to read into (must be large enough)
 * @param max_size Maximum bytes to read
 * @return Number of bytes read, or -1 on error
 */
int minix3_read_entire_file(minix3_fs_info_t *fs, struct minix3_inode *inode,
                            uint8_t *buf, uint32_t max_size)
{
    uint32_t size = inode->i_size;
    if (size > max_size) {
        printk("[minix3] File too large (%u bytes, buffer is %u)\n", size, max_size);
        size = max_size;
    }
    
    return minix3_read_file(fs, inode, 0, buf, size);
}

/**
 * Display file contents as text (for testing)
 * @param fs Filesystem info
 * @param inode File inode
 * @param max_bytes Maximum bytes to display
 */
void minix3_cat_file(minix3_fs_info_t *fs, struct minix3_inode *inode, uint32_t max_bytes)
{
    if (!MINIX3_ISREG(inode->i_mode)) {
        printk("[minix3] Not a regular file\n");
        return;
    }
    
    uint32_t size = inode->i_size;
    if (size > max_bytes) {
        size = max_bytes;
    }
    
    uint8_t buf[4096];
    if (size > sizeof(buf)) {
        size = sizeof(buf);
    }
    
    int bytes_read = minix3_read_file(fs, inode, 0, buf, size);
    if (bytes_read < 0) {
        printk("[minix3] Failed to read file\n");
        return;
    }
    
    /* Display as text */
    for (int i = 0; i < bytes_read; i++) {
        if (buf[i] >= 32 && buf[i] < 127) {
            printk("%c", buf[i]);
        } else if (buf[i] == '\n') {
            printk("\n");
        } else if (buf[i] == '\t') {
            printk("\t");
        } else {
            printk(".");
        }
    }
}