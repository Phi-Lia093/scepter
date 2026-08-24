/* ============================================================================
 * Minix v3 Filesystem - File Operations
 * ============================================================================ */

#include "fs/minix3.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "driver/char/rtc.h"
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

/**
 * Allocate a zone for a file block (with indirect block allocation if needed)
 * @param fs Filesystem info
 * @param inode Inode structure
 * @param file_block Block number within file (0-based)
 * @param zone_out Output: allocated zone number
 * @return 0 on success, -1 on error
 */
static int minix3_alloc_file_block(minix3_fs_info_t *fs, struct minix3_inode *inode,
                                    uint32_t file_block, uint32_t *zone_out)
{
    uint32_t zone_ptrs_per_block = fs->block_size / 4;
    
    /* Direct blocks (0-6) */
    if (file_block < MINIX3_DIRECT_ZONES) {
        if (inode->i_zone[file_block] == 0) {
            /* Allocate new zone */
            uint32_t new_zone;
            if (minix3_alloc_zone(fs, &new_zone) < 0) {
                return -1;
            }
            inode->i_zone[file_block] = new_zone;
        }
        *zone_out = inode->i_zone[file_block];
        return 0;
    }
    
    file_block -= MINIX3_DIRECT_ZONES;
    
    /* Single indirect (block 7) */
    if (file_block < zone_ptrs_per_block) {
        /* Allocate indirect block if needed */
        if (inode->i_zone[7] == 0) {
            uint32_t new_zone;
            if (minix3_alloc_zone(fs, &new_zone) < 0) {
                return -1;
            }
            inode->i_zone[7] = new_zone;
            /* Zero the indirect block */
            uint8_t zero_buf[4096] = {0};
            uint32_t sector = inode->i_zone[7] * (fs->block_size / 512);
            bwrite(fs->device_id, fs->partition_id, zero_buf, sector, fs->block_size / 512);
        }
        
        /* Read indirect block */
        uint8_t ind_buf[4096];
        uint32_t sector = inode->i_zone[7] * (fs->block_size / 512);
        int sectors = fs->block_size / 512;
        
        if (bread(fs->device_id, fs->partition_id, ind_buf, sector, sectors) < 0) {
            return -1;
        }
        
        uint32_t *zones = (uint32_t *)ind_buf;
        
        /* Allocate data zone if needed */
        if (zones[file_block] == 0) {
            if (minix3_alloc_zone(fs, &zones[file_block]) < 0) {
                return -1;
            }
            /* Write back indirect block */
            if (bwrite(fs->device_id, fs->partition_id, ind_buf, sector, sectors) < 0) {
                return -1;
            }
        }
        
        *zone_out = zones[file_block];
        return 0;
    }
    
    file_block -= zone_ptrs_per_block;
    
    /* Double indirect (block 8): file_block in [0, zone_ptrs_per_block^2) */
    if (file_block < zone_ptrs_per_block * zone_ptrs_per_block) {
        uint32_t ind1_index = file_block / zone_ptrs_per_block;
        uint32_t ind2_index = file_block % zone_ptrs_per_block;
        int sectors = fs->block_size / 512;
        
        /* Allocate the double-indirect block (level 1) if needed */
        if (inode->i_zone[8] == 0) {
            uint32_t new_zone;
            if (minix3_alloc_zone(fs, &new_zone) < 0) {
                return -1;
            }
            inode->i_zone[8] = new_zone;
            uint8_t zero_buf[4096] = {0};
            bwrite(fs->device_id, fs->partition_id, zero_buf,
                   new_zone * sectors, sectors);
        }
        
        /* Read level-1 block */
        uint8_t ind1_buf[4096];
        uint32_t sector = inode->i_zone[8] * sectors;
        if (bread(fs->device_id, fs->partition_id, ind1_buf, sector, sectors) < 0) {
            return -1;
        }
        uint32_t *zones1 = (uint32_t *)ind1_buf;
        
        /* Allocate the level-2 block if needed */
        if (zones1[ind1_index] == 0) {
            uint32_t new_zone;
            if (minix3_alloc_zone(fs, &new_zone) < 0) {
                return -1;
            }
            zones1[ind1_index] = new_zone;
            if (bwrite(fs->device_id, fs->partition_id, ind1_buf, sector, sectors) < 0) {
                return -1;
            }
            uint8_t zero_buf[4096] = {0};
            bwrite(fs->device_id, fs->partition_id, zero_buf,
                   new_zone * sectors, sectors);
        }
        
        /* Read level-2 block and allocate the data zone if needed */
        uint8_t ind2_buf[4096];
        uint32_t ind2_zone = zones1[ind1_index];
        uint32_t sector2 = ind2_zone * sectors;
        if (bread(fs->device_id, fs->partition_id, ind2_buf, sector2, sectors) < 0) {
            return -1;
        }
        uint32_t *zones2 = (uint32_t *)ind2_buf;
        if (zones2[ind2_index] == 0) {
            if (minix3_alloc_zone(fs, &zones2[ind2_index]) < 0) {
                return -1;
            }
            if (bwrite(fs->device_id, fs->partition_id, ind2_buf, sector2, sectors) < 0) {
                return -1;
            }
        }
        
        *zone_out = zones2[ind2_index];
        return 0;
    }
    
    /* Triple indirect not commonly used, return error */
    printk("[minix3] File too large for write (triple indirect not implemented)\n");
    return -1;
}

/**
 * Write data to a file
 * @param fs Filesystem info
 * @param inode File inode (will be modified)
 * @param offset Offset within file to start writing
 * @param buf Buffer containing data to write
 * @param count Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
int minix3_write_file(minix3_fs_info_t *fs, struct minix3_inode *inode,
                      uint32_t offset, const uint8_t *buf, uint32_t count)
{
    if (!MINIX3_ISREG(inode->i_mode)) {
        printk("[minix3] Not a regular file\n");
        return -1;
    }
    
    uint32_t bytes_written = 0;
    uint8_t block_buf[4096];
    
    while (bytes_written < count) {
        /* Calculate which block and offset within block */
        uint32_t file_block = (offset + bytes_written) / fs->block_size;
        uint32_t block_offset = (offset + bytes_written) % fs->block_size;
        uint32_t bytes_in_block = fs->block_size - block_offset;
        
        if (bytes_in_block > count - bytes_written) {
            bytes_in_block = count - bytes_written;
        }
        
        /* Get or allocate zone for this block */
        uint32_t zone;
        if (minix3_alloc_file_block(fs, inode, file_block, &zone) < 0) {
            return bytes_written > 0 ? (int)bytes_written : -1;
        }
        
        /* If partial block write, read existing data first.
         * A block "already has data" if it lies strictly within the file's
         * current size (file_block * block_size < i_size). This is true even
         * when appending at exactly EOF inside an existing block (offset ==
         * i_size) - e.g. echo writes "aaa" then "\n" as two separate write()
         * calls. The old check (offset + bytes_written < i_size) was false in
         * that case and zeroed the whole block, destroying the first write. */
        if (block_offset != 0 || bytes_in_block < fs->block_size) {
            uint32_t sector = zone * (fs->block_size / 512);
            int sectors = fs->block_size / 512;
            
            if ((file_block * fs->block_size) < inode->i_size) {
                if (bread(fs->device_id, fs->partition_id, block_buf, sector, sectors) < 0) {
                    return bytes_written > 0 ? (int)bytes_written : -1;
                }
            } else {
                /* New block beyond EOF, zero it */
                memset(block_buf, 0, fs->block_size);
            }
        }
        
        /* Copy data into block buffer */
        memcpy(block_buf + block_offset, buf + bytes_written, bytes_in_block);
        
        /* Write the block */
        uint32_t sector = zone * (fs->block_size / 512);
        int sectors = fs->block_size / 512;
        
        if (bwrite(fs->device_id, fs->partition_id, block_buf, sector, sectors) < 0) {
            printk("[minix3] Failed to write block\n");
            return bytes_written > 0 ? (int)bytes_written : -1;
        }
        
        bytes_written += bytes_in_block;
    }
    
    /* Update file size if we wrote beyond EOF */
    if (offset + bytes_written > inode->i_size) {
        inode->i_size = offset + bytes_written;
    }
    
    /* Update modification time with real timestamp */
    inode->i_mtime = rtc_get_unix_time();
    
    return bytes_written;
}

/**
 * Truncate a file to a specific size
 * @param fs Filesystem info
 * @param inode File inode (will be modified)
 * @param new_size New size for the file
 * @return 0 on success, -1 on error
 */
int minix3_truncate_file(minix3_fs_info_t *fs, struct minix3_inode *inode,
                         uint32_t new_size)
{
    if (!MINIX3_ISREG(inode->i_mode)) {
        printk("[minix3] Cannot truncate non-regular file\n");
        return -1;
    }
    
    uint32_t old_size = inode->i_size;
    
    /* If growing or same size, just update size (sparse file) */
    if (new_size >= old_size) {
        inode->i_size = new_size;
        inode->i_mtime = rtc_get_unix_time();
        return 0;
    }
    
    /* Shrinking - need to free blocks beyond new size */
    uint32_t old_blocks = (old_size + fs->block_size - 1) / fs->block_size;
    uint32_t new_blocks = (new_size + fs->block_size - 1) / fs->block_size;
    
    /* Free direct blocks */
    for (uint32_t i = new_blocks; i < old_blocks && i < MINIX3_DIRECT_ZONES; i++) {
        if (inode->i_zone[i] != 0) {
            minix3_free_zone(fs, inode->i_zone[i]);
            inode->i_zone[i] = 0;
        }
    }
    
    /* Free single indirect blocks if needed */
    if (old_blocks > MINIX3_DIRECT_ZONES) {
        uint32_t zone_ptrs_per_block = fs->block_size / 4;
        uint32_t indirect_start = MINIX3_DIRECT_ZONES;
        uint32_t indirect_end = MINIX3_DIRECT_ZONES + zone_ptrs_per_block;
        
        if (inode->i_zone[7] != 0) {
            /* Read indirect block */
            uint8_t ind_buf[4096];
            uint32_t sector = inode->i_zone[7] * (fs->block_size / 512);
            int sectors = fs->block_size / 512;
            
            if (bread(fs->device_id, fs->partition_id, ind_buf, sector, sectors) < 0) {
                printk("[minix3] Failed to read indirect block for truncate\n");
                return -1;
            }
            
            uint32_t *zones = (uint32_t *)ind_buf;
            int modified = 0;
            
            /* Free blocks beyond new size.  IMPORTANT: start at the first
             * index that is both >= new_blocks and inside the single
             * indirect range; otherwise (new_blocks < indirect_start) the
             * index underflows and reads garbage past the buffer. */
            uint32_t start = (new_blocks > indirect_start) ? new_blocks
                                                           : indirect_start;
            for (uint32_t i = start; i < old_blocks && i < indirect_end; i++) {
                uint32_t idx = i - indirect_start;
                if (zones[idx] != 0) {
                    minix3_free_zone(fs, zones[idx]);
                    zones[idx] = 0;
                    modified = 1;
                }
            }
            
            /* Write back indirect block if modified */
            if (modified) {
                if (bwrite(fs->device_id, fs->partition_id, ind_buf, sector, sectors) < 0) {
                    printk("[minix3] Failed to write indirect block\n");
                    return -1;
                }
            }
            
            /* If all indirect blocks freed, free the indirect block itself */
            if (new_blocks <= MINIX3_DIRECT_ZONES) {
                minix3_free_zone(fs, inode->i_zone[7]);
                inode->i_zone[7] = 0;
            }
        }
    }
    
    /* Free double indirect blocks (block 8) */
    {
        uint32_t zone_ptrs_per_block = fs->block_size / 4;
        uint32_t dbl_start = MINIX3_DIRECT_ZONES + zone_ptrs_per_block;
        uint32_t dbl_end   = dbl_start +
                             zone_ptrs_per_block * zone_ptrs_per_block;
        int sectors = fs->block_size / 512;

        if (inode->i_zone[8] != 0 && old_blocks > dbl_start &&
            new_blocks < dbl_end) {
            uint8_t ind1_buf[4096];
            uint32_t sector = inode->i_zone[8] * sectors;
            if (bread(fs->device_id, fs->partition_id, ind1_buf,
                      sector, sectors) < 0) {
                printk("[minix3] Failed to read double-indirect block "
                       "for truncate\n");
                return -1;
            }
            uint32_t *zones1 = (uint32_t *)ind1_buf;
            int modified = 0;

            for (uint32_t i = 0; i < zone_ptrs_per_block; i++) {
                if (zones1[i] == 0)
                    continue;

                uint32_t ind2_zone = zones1[i];
                uint8_t ind2_buf[4096];
                uint32_t s2 = ind2_zone * sectors;
                if (bread(fs->device_id, fs->partition_id, ind2_buf,
                          s2, sectors) < 0) {
                    printk("[minix3] Failed to read level-2 block\n");
                    return -1;
                }
                uint32_t *zones2 = (uint32_t *)ind2_buf;
                int mod2 = 0;

                /* Free data zones whose file block is past new_size */
                for (uint32_t k = 0; k < zone_ptrs_per_block; k++) {
                    uint32_t fblock = dbl_start +
                                      i * zone_ptrs_per_block + k;
                    if (fblock >= new_blocks && zones2[k] != 0) {
                        minix3_free_zone(fs, zones2[k]);
                        zones2[k] = 0;
                        mod2 = 1;
                    }
                }
                if (mod2) {
                    if (bwrite(fs->device_id, fs->partition_id, ind2_buf,
                               s2, sectors) < 0) {
                        return -1;
                    }
                }

                /* If the whole level-2 block is now empty, free it */
                int empty2 = 1;
                for (uint32_t k = 0; k < zone_ptrs_per_block; k++) {
                    if (zones2[k] != 0) {
                        empty2 = 0;
                        break;
                    }
                }
                if (empty2) {
                    minix3_free_zone(fs, ind2_zone);
                    zones1[i] = 0;
                    modified = 1;
                }
            }

            /* If the whole level-1 block is empty, free it too */
            int empty1 = 1;
            for (uint32_t i = 0; i < zone_ptrs_per_block; i++) {
                if (zones1[i] != 0) {
                    empty1 = 0;
                    break;
                }
            }
            if (empty1) {
                minix3_free_zone(fs, inode->i_zone[8]);
                inode->i_zone[8] = 0;
            } else if (modified) {
                if (bwrite(fs->device_id, fs->partition_id, ind1_buf,
                           sector, sectors) < 0) {
                    return -1;
                }
            }
        }
    }
    
    /* Update inode */
    inode->i_size = new_size;
    inode->i_mtime = rtc_get_unix_time();
    
    return 0;
}

