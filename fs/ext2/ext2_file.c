/* ============================================================================
 * ext2 Filesystem - File Operations
 * ============================================================================ */

#include "fs/ext2.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "driver/char/rtc.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "lib/string.h"

/* ============================================================================
 * Read
 * ============================================================================ */

/**
 * Read `count` bytes from `offset` into buf.  Sparse holes read as zeros.
 * Returns bytes read (may be less than count near EOF), or -1 on error.
 */
int ext2_read_file(ext2_fs_info_t *fs, struct ext2_inode *inode,
                   uint32_t offset, uint8_t *buf, uint32_t count)
{
    /* Fast symlinks: targets <= 60 bytes are stored directly in i_block[]. */
    if (EXT2_ISLNK(inode->i_mode) && inode->i_size <= 60 &&
        inode->i_blocks == 0) {
        uint32_t len = inode->i_size;
        if (offset >= len)
            return 0;
        if (count > len - offset)
            count = len - offset;
        memcpy(buf, (const uint8_t *)inode->i_block + offset, count);
        return (int)count;
    }

    if (offset >= inode->i_size)
        return 0;
    if (count > inode->i_size - offset)
        count = inode->i_size - offset;

    uint32_t bytes = 0;
    uint8_t block_buf[4096];

    while (bytes < count) {
        uint32_t file_block = (offset + bytes) / fs->block_size;
        uint32_t block_off  = (offset + bytes) % fs->block_size;
        uint32_t chunk = count - bytes;
        if (chunk > fs->block_size - block_off)
            chunk = fs->block_size - block_off;

        uint32_t dblk;
        if (ext2_bmap(fs, inode, file_block, &dblk) < 0)
            return -1;

        if (dblk == 0) {
            memset(block_buf, 0, fs->block_size);   /* hole */
        } else {
            if (ext2_read_block(fs, dblk, block_buf) < 0)
                return -1;
        }

        memcpy(buf + bytes, block_buf + block_off, chunk);
        bytes += chunk;
    }

    return (int)bytes;
}

/* ============================================================================
 * Write
 * ============================================================================ */

/**
 * Write `count` bytes at `offset`.  Allocates data blocks (and indirect
 * blocks) as needed.  Updates the in-memory inode (i_size, i_blocks,
 * i_block pointers, mtime); the caller writes the inode back.
 * Returns bytes written, or -1 on error.
 */
int ext2_write_file(ext2_fs_info_t *fs, uint32_t ino, struct ext2_inode *inode,
                    uint32_t offset, const uint8_t *buf, uint32_t count)
{
    uint32_t bytes = 0;
    uint8_t block_buf[4096];
    int inode_changed = 0;

    while (bytes < count) {
        uint32_t file_block = (offset + bytes) / fs->block_size;
        uint32_t block_off  = (offset + bytes) % fs->block_size;
        uint32_t chunk = count - bytes;
        if (chunk > fs->block_size - block_off)
            chunk = fs->block_size - block_off;

        uint32_t dblk;
        if (ext2_bmap(fs, inode, file_block, &dblk) < 0)
            return -1;

        int partial = (block_off != 0 || chunk != fs->block_size);

        if (dblk == 0) {
            /* Hole: zero-fill (read-modify-write only matters for existing
             * data, which by definition a hole does not have). */
            memset(block_buf, 0, fs->block_size);
            if (ext2_bmap_alloc(fs, ino, inode, file_block, &dblk) < 0)
                return -1;
            inode_changed = 1;
        } else if (partial) {
            /* Partial overwrite of an existing block: read first. */
            if (ext2_read_block(fs, dblk, block_buf) < 0)
                return -1;
        } else {
            memset(block_buf, 0, fs->block_size);
        }

        memcpy(block_buf + block_off, buf + bytes, chunk);
        if (ext2_write_block(fs, dblk, block_buf) < 0)
            return -1;
        bytes += chunk;
    }

    if (offset + count > inode->i_size) {
        inode->i_size = offset + count;
        inode_changed = 1;
    }
    if (inode_changed) {
        inode->i_mtime = rtc_get_unix_time();
        ext2_refresh_blocks(fs, inode);
    }
    return (int)bytes;
}

/* ============================================================================
 * Truncate
 * ============================================================================ */

/**
 * Truncate a file to `new_size`.  Shrinking frees the excess data blocks
 * (and any emptied indirect blocks); extending only updates i_size (the
 * new region is a sparse hole).  Updates the in-memory inode; the caller
 * writes it back.
 */
int ext2_truncate_file(ext2_fs_info_t *fs, uint32_t ino,
                       struct ext2_inode *inode, uint32_t new_size)
{
    (void)ino;   /* block freeing is driven purely by the pointer tree */
    uint32_t old_blocks = (inode->i_size + fs->block_size - 1) / fs->block_size;
    uint32_t new_blocks = (new_size + fs->block_size - 1) / fs->block_size;

    if (new_blocks < old_blocks) {
        if (ext2_truncate_blocks(fs, inode, new_blocks, old_blocks) < 0)
            return -1;
    }

    inode->i_size = new_size;
    inode->i_mtime = rtc_get_unix_time();
    inode->i_ctime = inode->i_mtime;
    ext2_refresh_blocks(fs, inode);
    return 0;
}

/* ============================================================================
 * stat
 * ============================================================================ */

/**
 * Fill a VFS stat_t from an ext2 inode.  Shared by stat/fstat/readdir.
 */
void ext2_fill_stat(const struct ext2_inode *inode, uint32_t ino, stat_t *st)
{
    st->size  = inode->i_size;
    st->inode = ino;
    st->ctime = inode->i_ctime;
    st->mtime = inode->i_mtime;
    st->mode  = inode->i_mode;
    st->uid   = inode->i_uid;
    st->gid   = inode->i_gid;

    if (EXT2_ISDIR(inode->i_mode)) {
        st->type = DT_DIR;
    } else if (EXT2_ISREG(inode->i_mode)) {
        st->type = DT_REG;
    } else if (EXT2_ISCHR(inode->i_mode)) {
        st->type = DT_CHRDEV;
    } else if (EXT2_ISBLK(inode->i_mode)) {
        st->type = DT_BLKDEV;
    } else if (EXT2_ISLNK(inode->i_mode)) {
        st->type = DT_SYMLINK;
    } else if (EXT2_ISFIFO(inode->i_mode)) {
        st->type = DT_FIFO;
    } else if (EXT2_ISSOCK(inode->i_mode)) {
        st->type = DT_UNKNOWN;   /* VFS has no DT_SOCK */
    } else {
        st->type = DT_UNKNOWN;
    }
}
