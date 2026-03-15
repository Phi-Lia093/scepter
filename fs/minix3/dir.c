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
            
            /* Read entry inode to get file type */
            struct minix3_inode entry_inode;
            if (minix3_read_inode(fs, entries[i].inode, &entry_inode) < 0) {
                printk("  inode=%5u  <error reading inode>  %s\n",
                       entries[i].inode, entries[i].name);
                continue;
            }
            
            /* Determine file type */
            const char *type;
            if (MINIX3_ISDIR(entry_inode.i_mode)) {
                type = "DIR ";
            } else if (MINIX3_ISREG(entry_inode.i_mode)) {
                type = "FILE";
            } else if (MINIX3_ISCHR(entry_inode.i_mode)) {
                type = "CHR ";
            } else if (MINIX3_ISBLK(entry_inode.i_mode)) {
                type = "BLK ";
            } else if (MINIX3_ISLNK(entry_inode.i_mode)) {
                type = "LNK ";
            } else {
                type = "????";
            }
            
            total++;
        }
    }
    
    return 0;
}