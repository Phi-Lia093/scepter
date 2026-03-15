/* ============================================================================
 * Minix v3 Filesystem - Directory Operations
 * ============================================================================ */

#include "fs/minix3.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "lib/printk.h"
#include "lib/string.h"

/**
 * Read a directory block and parse entries
 * @param fs Filesystem info
 * @param inode Directory inode
 * @param block_num Block number within directory (0-based)
 * @param entries Output buffer for directory entries (must hold block_size/64 entries)
 * @param count Output: number of entries read
 * @return 0 on success, -1 on error
 */
static int minix3_read_dir_block(minix3_fs_info_t *fs, struct minix3_inode *inode,
                                  uint32_t block_num, struct minix3_dirent *entries,
                                  int *count)
{
    /* Map file block to zone */
    uint32_t zone;
    if (minix3_bmap(fs, inode, block_num, &zone) < 0) {
        return -1;
    }
    
    /* Sparse block or past end of file */
    if (zone == 0) {
        *count = 0;
        return 0;
    }
    
    /* Read the zone */
    uint8_t buf[4096];  /* Support up to 4KB blocks */
    uint32_t sector = zone * (fs->block_size / 512);
    int sectors = fs->block_size / 512;
    
    if (bread(fs->device_id, fs->partition_id, buf, sector, sectors) < 0) {
        printk("[minix3] Failed to read directory block\n");
        return -1;
    }
    
    /* Parse directory entries */
    int entries_per_block = fs->block_size / MINIX3_DIRENT_SIZE;
    *count = entries_per_block;
    
    for (int i = 0; i < entries_per_block; i++) {
        struct minix3_dirent *src = (struct minix3_dirent *)(buf + i * MINIX3_DIRENT_SIZE);
        memcpy(&entries[i], src, MINIX3_DIRENT_SIZE);
    }
    
    return 0;
}

/**
 * Add a directory entry
 * @param fs Filesystem info
 * @param dir_inode Directory inode (will be modified if directory grows)
 * @param name Filename to add
 * @param ino Inode number for the entry
 * @return 0 on success, -1 on error
 */
int minix3_add_dirent(minix3_fs_info_t *fs, struct minix3_inode *dir_inode,
                      const char *name, uint32_t ino)
{
    if (!MINIX3_ISDIR(dir_inode->i_mode)) {
        printk("[minix3] Not a directory\n");
        return -1;
    }
    
    if (strlen(name) >= MINIX3_NAME_LEN) {
        printk("[minix3] Filename too long\n");
        return -1;
    }
    
    uint32_t num_blocks = (dir_inode->i_size + fs->block_size - 1) / fs->block_size;
    int entries_per_block = fs->block_size / MINIX3_DIRENT_SIZE;
    struct minix3_dirent entries[entries_per_block];
    
    /* Search for an empty slot in existing blocks */
    for (uint32_t block = 0; block < num_blocks; block++) {
        int count;
        if (minix3_read_dir_block(fs, dir_inode, block, entries, &count) < 0) {
            return -1;
        }
        
        /* Look for deleted/unused entry */
        for (int i = 0; i < count; i++) {
            if (entries[i].inode == 0) {
                /* Found empty slot - use it */
                entries[i].inode = ino;
                strncpy(entries[i].name, name, MINIX3_NAME_LEN);
                entries[i].name[MINIX3_NAME_LEN - 1] = '\0';
                
                /* Write block back */
                uint32_t zone;
                if (minix3_bmap(fs, dir_inode, block, &zone) < 0) {
                    return -1;
                }
                
                uint8_t buf[4096];
                memcpy(buf, entries, fs->block_size);
                uint32_t sector = zone * (fs->block_size / 512);
                int sectors = fs->block_size / 512;
                
                if (bwrite(fs->device_id, fs->partition_id, buf, sector, sectors) < 0) {
                    printk("[minix3] Failed to write directory block\n");
                    return -1;
                }
                
                /* Update directory size if we wrote beyond current size
                 * ls only reads up to i_size, so we must update it! */
                uint32_t entry_end_offset = (block * fs->block_size) + ((i + 1) * MINIX3_DIRENT_SIZE);
                if (entry_end_offset > dir_inode->i_size) {
                    dir_inode->i_size = entry_end_offset;
                    dir_inode->i_mtime++;
                }
                
                return 0;
            }
        }
    }
    
    /* No empty slots - need to grow directory */
    uint32_t new_block = num_blocks;
    
    /* Allocate a new block for the directory */
    uint32_t zone;
    if (new_block < MINIX3_DIRECT_ZONES) {
        if (minix3_alloc_zone(fs, &dir_inode->i_zone[new_block]) < 0) {
            return -1;
        }
        zone = dir_inode->i_zone[new_block];
    } else {
        printk("[minix3] Directory too large (indirect not yet supported)\n");
        return -1;
    }
    
    /* Create new directory block with our entry */
    uint8_t buf[4096];
    memset(buf, 0, fs->block_size);
    
    struct minix3_dirent *new_entry = (struct minix3_dirent *)buf;
    new_entry->inode = ino;
    strncpy(new_entry->name, name, MINIX3_NAME_LEN);
    new_entry->name[MINIX3_NAME_LEN - 1] = '\0';
    
    /* Write new block */
    uint32_t sector = zone * (fs->block_size / 512);
    int sectors = fs->block_size / 512;
    
    if (bwrite(fs->device_id, fs->partition_id, buf, sector, sectors) < 0) {
        printk("[minix3] Failed to write new directory block\n");
        minix3_free_zone(fs, zone);
        dir_inode->i_zone[new_block] = 0;
        return -1;
    }
    
    /* Update directory size */
    dir_inode->i_size += fs->block_size;
    dir_inode->i_mtime++;
    
    return 0;
}

/**
 * Remove a directory entry
 * @param fs Filesystem info
 * @param dir_inode Directory inode
 * @param name Filename to remove
 * @return 0 on success, -1 on error
 */
int minix3_remove_dirent(minix3_fs_info_t *fs, struct minix3_inode *dir_inode,
                         const char *name)
{
    if (!MINIX3_ISDIR(dir_inode->i_mode)) {
        printk("[minix3] Not a directory\n");
        return -1;
    }
    
    uint32_t num_blocks = (dir_inode->i_size + fs->block_size - 1) / fs->block_size;
    int entries_per_block = fs->block_size / MINIX3_DIRENT_SIZE;
    struct minix3_dirent entries[entries_per_block];
    
    /* Search for the entry */
    for (uint32_t block = 0; block < num_blocks; block++) {
        int count;
        if (minix3_read_dir_block(fs, dir_inode, block, entries, &count) < 0) {
            return -1;
        }
        
        for (int i = 0; i < count; i++) {
            if (entries[i].inode != 0 && 
                strncmp(entries[i].name, name, MINIX3_NAME_LEN) == 0) {
                /* Found it - mark as deleted */
                entries[i].inode = 0;
                memset(entries[i].name, 0, MINIX3_NAME_LEN);
                
                /* Write block back */
                uint32_t zone;
                if (minix3_bmap(fs, dir_inode, block, &zone) < 0) {
                    return -1;
                }
                
                uint8_t buf[4096];
                memcpy(buf, entries, fs->block_size);
                uint32_t sector = zone * (fs->block_size / 512);
                int sectors = fs->block_size / 512;
                
                if (bwrite(fs->device_id, fs->partition_id, buf, sector, sectors) < 0) {
                    printk("[minix3] Failed to write directory block\n");
                    return -1;
                }
                
                dir_inode->i_mtime++;
                return 0;
            }
        }
    }
    
    /* Not found */
    return -1;
}

/**
 * Look up a filename in a directory
 * @param fs Filesystem info
 * @param dir_inode Directory inode
 * @param name Filename to search for
 * @param inode_out Output: inode number of found entry
 * @return 0 on success, -1 if not found or error
 */
int minix3_lookup(minix3_fs_info_t *fs, struct minix3_inode *dir_inode,
                  const char *name, uint32_t *inode_out)
{
    if (!MINIX3_ISDIR(dir_inode->i_mode)) {
        printk("[minix3] lookup: not a directory\n");
        return -1;
    }
    
    uint32_t num_blocks = (dir_inode->i_size + fs->block_size - 1) / fs->block_size;
    int entries_per_block = fs->block_size / MINIX3_DIRENT_SIZE;
    
    struct minix3_dirent entries[entries_per_block];
    
    /* Search through all directory blocks */
    for (uint32_t block = 0; block < num_blocks; block++) {
        int count;
        if (minix3_read_dir_block(fs, dir_inode, block, entries, &count) < 0) {
            return -1;
        }
        
        /* Check each entry in this block */
        for (int i = 0; i < count; i++) {
            /* Skip deleted/unused entries */
            if (entries[i].inode == 0) {
                continue;
            }
            
            /* Compare filename */
            if (strncmp(entries[i].name, name, MINIX3_NAME_LEN) == 0) {
                *inode_out = entries[i].inode;
                return 0;
            }
        }
    }
    
    /* Not found */
    return -1;
}

/**
 * List all entries in a directory
 * @param fs Filesystem info
 * @param dir_inode Directory inode
 * @return 0 on success, -1 on error
 */
int minix3_list_dir(minix3_fs_info_t *fs, struct minix3_inode *dir_inode)
{
    if (!MINIX3_ISDIR(dir_inode->i_mode)) {
        printk("[minix3] Not a directory\n");
        return -1;
    }
    
    uint32_t num_blocks = (dir_inode->i_size + fs->block_size - 1) / fs->block_size;
    int entries_per_block = fs->block_size / MINIX3_DIRENT_SIZE;
    
    struct minix3_dirent entries[entries_per_block];
    
    int total = 0;
    
    /* Read and display all directory blocks */
    for (uint32_t block = 0; block < num_blocks; block++) {
        int count;
        if (minix3_read_dir_block(fs, dir_inode, block, entries, &count) < 0) {
            return -1;
        }
        
        /* Display each entry */
        for (int i = 0; i < count; i++) {
            /* Skip deleted/unused entries */
            if (entries[i].inode == 0) {
                continue;
            }
            
            /* Just count entries without printing */
            total++;
        }
    }
    
    return 0;
}