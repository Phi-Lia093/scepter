/* ============================================================================
 * Minix v3 Filesystem Driver - VFS Integration
 * ============================================================================ */

#include "fs/minix3.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "lib/string.h"

/* ============================================================================
 * VFS Callback: open
 * ============================================================================ */

static int minix3_vfs_open(void *fs_private, const char *path, int flags,
                           void **file_private)
{
    minix3_fs_info_t *fs = (minix3_fs_info_t *)fs_private;
    if (!fs || !path || !file_private) return -1;
    
    /* Allocate file info structure */
    minix3_file_info_t *file = (minix3_file_info_t *)kalloc(sizeof(minix3_file_info_t));
    if (!file) {
        printk("[minix3] Failed to allocate file info\n");
        return -1;
    }
    
    file->fs = fs;
    file->offset = 0;
    file->dir_pos = 0;
    file->dirty = 0;
    
    /* Parse path - for now we only support simple paths from root */
    const char *filename = path;
    if (filename[0] == '/') filename++;  /* Skip leading slash */
    
    /* Handle root directory specially */
    if (filename[0] == '\0' || strcmp(filename, ".") == 0) {
        file->inode_num = MINIX3_ROOT_INO;
        if (minix3_read_inode(fs, MINIX3_ROOT_INO, &file->inode) < 0) {
            kfree(file);
            return -1;
        }
        *file_private = file;
        return 0;
    }
    
    /* Navigate path components */
    struct minix3_inode dir_inode;
    uint32_t current_ino = MINIX3_ROOT_INO;
    
    if (minix3_read_inode(fs, current_ino, &dir_inode) < 0) {
        kfree(file);
        return -1;
    }
    
    /* Parse path by '/' separators */
    char path_copy[256];
    strncpy(path_copy, filename, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    char *token = path_copy;
    char *next = token;
    
    while (*token) {
        /* Find next '/' or end of string */
        while (*next && *next != '/') next++;
        
        int is_last = (*next == '\0');
        *next = '\0';  /* Temporarily terminate */
        
        /* Skip empty components */
        if (token[0] == '\0') {
            if (!is_last) {
                token = next + 1;
                next = token;
            }
            continue;
        }
        
        /* Lookup component in current directory */
        uint32_t next_ino;
        if (minix3_lookup(fs, &dir_inode, token, &next_ino) < 0) {
            /* Component not found */
            if (is_last && (flags & O_CREAT)) {
                /* Create new file */
                uint16_t mode = MINIX3_S_IFREG | MINIX3_S_IRUSR | MINIX3_S_IWUSR | 
                               MINIX3_S_IRGRP | MINIX3_S_IROTH;  /* 0644 */
                
                /* Allocate new inode */
                if (minix3_alloc_inode(fs, mode, &next_ino) < 0) {
                    printk("[minix3] Failed to allocate inode\n");
                    kfree(file);
                    return -1;
                }
                
                /* Add directory entry */
                if (minix3_add_dirent(fs, &dir_inode, token, next_ino) < 0) {
                    printk("[minix3] Failed to add directory entry\n");
                    minix3_free_inode(fs, next_ino);
                    kfree(file);
                    return -1;
                }
                
                /* Write back parent directory inode (size may have changed) */
                if (minix3_write_inode(fs, current_ino, &dir_inode) < 0) {
                    printk("[minix3] Failed to write parent directory inode\n");
                }
                
                /* Sync bitmaps */
                minix3_sync_bitmaps(fs);
                
                /* Now read the newly created file's inode (use different variable) */
                struct minix3_inode new_file_inode;
                if (minix3_read_inode(fs, next_ino, &new_file_inode) < 0) {
                    kfree(file);
                    return -1;
                }
                
                /* Set up file handle with new file */
                file->inode_num = next_ino;
                file->inode = new_file_inode;
                *file_private = file;
                return 0;  /* File created and opened successfully */
            } else {
                /* File not found and O_CREAT not set */
                kfree(file);
                return -1;
            }
        }
        
        current_ino = next_ino;
        
        /* Read the inode for next iteration (or final result) */
        if (minix3_read_inode(fs, current_ino, &dir_inode) < 0) {
            kfree(file);
            return -1;
        }
        
        /* If not last component, must be a directory */
        if (!is_last && !MINIX3_ISDIR(dir_inode.i_mode)) {
            kfree(file);
            return -1;  /* Not a directory */
        }
        
        if (is_last) break;
        
        token = next + 1;
        next = token;
    }
    
    /* Found the target inode */
    file->inode_num = current_ino;
    file->inode = dir_inode;
    
    /* Handle O_TRUNC flag - truncate file to 0 if opened for writing */
    if ((flags & O_TRUNC) && MINIX3_ISREG(file->inode.i_mode)) {
        if (minix3_truncate_file(fs, &file->inode, 0) < 0) {
            kfree(file);
            return -1;
        }
        file->dirty = 1;  /* Mark inode as dirty */
    }
    
    *file_private = file;
    return 0;
}

/* ============================================================================
 * VFS Callback: close
 * ============================================================================ */

static int minix3_vfs_close(void *file_private)
{
    if (!file_private) return -1;
    
    minix3_file_info_t *file = (minix3_file_info_t *)file_private;
    
    /* Write back dirty inode if needed */
    if (file->dirty) {
        if (minix3_write_inode(file->fs, file->inode_num, &file->inode) < 0) {
            printk("[minix3] Failed to write back inode on close\n");
        }
        
        /* Sync bitmaps if dirty */
        if (minix3_sync_bitmaps(file->fs) < 0) {
            printk("[minix3] Failed to sync bitmaps on close\n");
        }
    }
    
    kfree(file);
    return 0;
}

/* ============================================================================
 * VFS Callback: read
 * ============================================================================ */

static int minix3_vfs_read(void *file_private, void *buf, size_t count)
{
    if (!file_private || !buf) return -1;
    
    minix3_file_info_t *file = (minix3_file_info_t *)file_private;
    
    /* Only regular files can be read */
    if (!MINIX3_ISREG(file->inode.i_mode)) {
        return -1;
    }
    
    /* Read from file at current offset */
    int bytes_read = minix3_read_file(file->fs, &file->inode, 
                                      file->offset, (uint8_t *)buf, count);
    
    if (bytes_read > 0) {
        file->offset += bytes_read;
    }
    
    return bytes_read;
}

/* ============================================================================
 * VFS Callback: write
 * ============================================================================ */

static int minix3_vfs_write(void *file_private, const void *buf, size_t count)
{
    if (!file_private || !buf) return -1;
    
    minix3_file_info_t *file = (minix3_file_info_t *)file_private;
    
    /* Only regular files can be written */
    if (!MINIX3_ISREG(file->inode.i_mode)) {
        return -1;
    }
    
    /* Write to file at current offset */
    int bytes_written = minix3_write_file(file->fs, &file->inode,
                                          file->offset, (const uint8_t *)buf, count);
    
    if (bytes_written > 0) {
        file->offset += bytes_written;
        file->dirty = 1;  /* Mark inode as dirty */
    }
    
    return bytes_written;
}

/* ============================================================================
 * VFS Callback: seek
 * ============================================================================ */

static int minix3_vfs_seek(void *file_private, int32_t offset, int whence,
                           uint32_t *new_offset)
{
    if (!file_private || !new_offset) return -1;
    
    minix3_file_info_t *file = (minix3_file_info_t *)file_private;
    
    int32_t target;
    
    switch (whence) {
    case SEEK_SET:
        target = offset;
        break;
    case SEEK_CUR:
        target = file->offset + offset;
        break;
    case SEEK_END:
        target = file->inode.i_size + offset;
        break;
    default:
        return -1;
    }
    
    if (target < 0) return -1;
    
    file->offset = (uint32_t)target;
    *new_offset = file->offset;
    return 0;
}

/* ============================================================================
 * VFS Callback: truncate
 * ============================================================================ */

static int minix3_vfs_truncate(void *file_private, uint32_t length)
{
    if (!file_private) return -1;
    
    minix3_file_info_t *file = (minix3_file_info_t *)file_private;
    
    /* Truncate the file */
    if (minix3_truncate_file(file->fs, &file->inode, length) < 0) {
        return -1;
    }
    
    /* Mark inode as dirty */
    file->dirty = 1;
    
    /* Adjust offset if it's beyond new size */
    if (file->offset > length) {
        file->offset = length;
    }
    
    return 0;
}

/* ============================================================================
 * VFS Callback: readdir
 * ============================================================================ */

static int minix3_vfs_readdir(void *file_private, dirent_t *dirent)
{
    if (!file_private || !dirent) return -1;
    
    minix3_file_info_t *file = (minix3_file_info_t *)file_private;
    
    /* Must be a directory */
    if (!MINIX3_ISDIR(file->inode.i_mode)) {
        return -1;
    }
    
    uint32_t dir_size = file->inode.i_size;
    uint32_t entries_per_block = file->fs->block_size / MINIX3_DIRENT_SIZE;
    uint32_t total_entries = dir_size / MINIX3_DIRENT_SIZE;
    
    /* Check if we've read all entries */
    if (file->dir_pos >= total_entries) {
        return 0;  /* End of directory */
    }
    
    /* Calculate which block and entry index */
    uint32_t block_num = file->dir_pos / entries_per_block;
    uint32_t entry_in_block = file->dir_pos % entries_per_block;
    
    /* Map block to zone */
    uint32_t zone;
    if (minix3_bmap(file->fs, &file->inode, block_num, &zone) < 0) {
        return -1;
    }
    
    if (zone == 0) {
        /* Sparse block - skip */
        file->dir_pos++;
        return minix3_vfs_readdir(file_private, dirent);  /* Try next entry */
    }
    
    /* Read the block */
    uint8_t buf[4096];
    uint32_t sector = zone * (file->fs->block_size / 512);
    int sectors = file->fs->block_size / 512;
    
    if (bread(file->fs->device_id, file->fs->partition_id, buf, sector, sectors) < 0) {
        return -1;
    }
    
    /* Extract the directory entry */
    struct minix3_dirent *entry = 
        (struct minix3_dirent *)(buf + entry_in_block * MINIX3_DIRENT_SIZE);
    
    /* Skip deleted entries */
    if (entry->inode == 0) {
        file->dir_pos++;
        return minix3_vfs_readdir(file_private, dirent);  /* Try next entry */
    }
    
    /* Fill dirent structure */
    strncpy(dirent->name, entry->name, sizeof(dirent->name) - 1);
    dirent->name[sizeof(dirent->name) - 1] = '\0';
    dirent->inode = entry->inode;
    
    /* Determine type by reading inode */
    struct minix3_inode entry_inode;
    if (minix3_read_inode(file->fs, entry->inode, &entry_inode) == 0) {
        if (MINIX3_ISDIR(entry_inode.i_mode)) {
            dirent->type = DT_DIR;
        } else if (MINIX3_ISREG(entry_inode.i_mode)) {
            dirent->type = DT_REG;
        } else if (MINIX3_ISCHR(entry_inode.i_mode)) {
            dirent->type = DT_CHRDEV;
        } else if (MINIX3_ISBLK(entry_inode.i_mode)) {
            dirent->type = DT_BLKDEV;
        } else if (MINIX3_ISLNK(entry_inode.i_mode)) {
            dirent->type = DT_SYMLINK;
        } else {
            dirent->type = DT_UNKNOWN;
        }
    } else {
        dirent->type = DT_UNKNOWN;
    }
    
    file->dir_pos++;
    return 1;  /* Entry returned */
}

/* ============================================================================
 * VFS Callback: stat
 * ============================================================================ */

static int minix3_vfs_stat(void *fs_private, const char *path, stat_t *st)
{
    if (!fs_private || !path || !st) return -1;
    
    /* Open the file to get its inode */
    void *file_private = NULL;
    if (minix3_vfs_open(fs_private, path, O_RDONLY, &file_private) < 0) {
        return -1;
    }
    
    minix3_file_info_t *file = (minix3_file_info_t *)file_private;
    
    /* Fill stat structure */
    st->size = file->inode.i_size;
    st->inode = file->inode_num;
    st->ctime = file->inode.i_ctime;
    st->mtime = file->inode.i_mtime;
    st->mode = file->inode.i_mode;
    
    /* Determine type */
    if (MINIX3_ISDIR(file->inode.i_mode)) {
        st->type = DT_DIR;
    } else if (MINIX3_ISREG(file->inode.i_mode)) {
        st->type = DT_REG;
    } else if (MINIX3_ISCHR(file->inode.i_mode)) {
        st->type = DT_CHRDEV;
    } else if (MINIX3_ISBLK(file->inode.i_mode)) {
        st->type = DT_BLKDEV;
    } else if (MINIX3_ISLNK(file->inode.i_mode)) {
        st->type = DT_SYMLINK;
    } else {
        st->type = DT_UNKNOWN;
    }
    
    minix3_vfs_close(file_private);
    return 0;
}

/* ============================================================================
 * Filesystem Operations Table
 * ============================================================================ */

static fs_ops_t minix3_ops = {
    .mount    = minix3_mount,
    .unmount  = minix3_unmount,
    .open     = minix3_vfs_open,
    .close    = minix3_vfs_close,
    .read     = minix3_vfs_read,
    .write    = minix3_vfs_write,
    .seek     = minix3_vfs_seek,
    .truncate = minix3_vfs_truncate,
    .readdir  = minix3_vfs_readdir,
    .mkdir    = NULL,  /* TODO: Implement mkdir */
    .rmdir    = NULL,  /* TODO: Implement rmdir */
    .unlink   = NULL,  /* TODO: Implement unlink */
    .rename   = NULL,  /* TODO: Implement rename */
    .stat     = minix3_vfs_stat,
};

/* ============================================================================
 * Initialization
 * ============================================================================ */

void minix3_init(void)
{
    if (register_filesystem("minix3", &minix3_ops) < 0) {
        printk("[minix3] Failed to register filesystem\n");
        return;
    }
    
    printk("[minix3] Minix v3 filesystem driver registered\n");
}