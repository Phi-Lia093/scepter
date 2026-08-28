/* ============================================================================
 * ext2 Filesystem - Inode Operations
 * ============================================================================ */

#include "fs/ext2.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "driver/char/rtc.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "lib/string.h"

/* ============================================================================
 * Inode read / write
 * ============================================================================ */

/**
 * Read an inode from disk.
 * @param ino Inode number (1-based)
 */
int ext2_read_inode(ext2_fs_info_t *fs, uint32_t ino, struct ext2_inode *inode)
{
    if (ino < 1 || ino > fs->sb.s_inodes_count) {
        printk("[ext2] Invalid inode number: %u\n", ino);
        return -1;
    }

    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    if (group >= fs->num_groups) {
        printk("[ext2] Inode %u group %u out of range\n", ino, group);
        return -1;
    }

    uint32_t table_block = fs->gdt[group].bg_inode_table;
    if (table_block == 0) {
        printk("[ext2] Group %u has no inode table\n", group);
        return -1;
    }

    uint32_t offset = index * fs->inode_size;
    uint32_t block = table_block + offset / fs->block_size;
    uint32_t in_block = offset % fs->block_size;

    uint8_t buf[4096];
    if (ext2_read_block(fs, block, buf) < 0) {
        printk("[ext2] Failed to read inode %u\n", ino);
        return -1;
    }

    memcpy(inode, buf + in_block, sizeof(struct ext2_inode));
    return 0;
}

/**
 * Write an inode back to disk (read-modify-write preserves the extra bytes
 * beyond the 128-byte base, e.g. i_extra_isize / checksums).
 */
int ext2_write_inode(ext2_fs_info_t *fs, uint32_t ino, struct ext2_inode *inode)
{
    if (ino < 1 || ino > fs->sb.s_inodes_count) {
        printk("[ext2] Invalid inode number: %u\n", ino);
        return -1;
    }

    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    if (group >= fs->num_groups) return -1;

    uint32_t table_block = fs->gdt[group].bg_inode_table;
    if (table_block == 0) return -1;

    uint32_t offset = index * fs->inode_size;
    uint32_t block = table_block + offset / fs->block_size;
    uint32_t in_block = offset % fs->block_size;

    uint8_t buf[4096];
    if (ext2_read_block(fs, block, buf) < 0) return -1;
    memcpy(buf + in_block, inode, sizeof(struct ext2_inode));
    if (ext2_write_block(fs, block, buf) < 0) return -1;
    return 0;
}

/* ============================================================================
 * Block mapping (file block -> disk block)
 *
 * i_block[0..11]   direct blocks
 * i_block[12]      single indirect
 * i_block[13]      double indirect
 * i_block[14]      triple indirect
 * ============================================================================ */

static inline uint32_t ext2_ptrs_per_block(ext2_fs_info_t *fs)
{
    return fs->block_size / 4;
}

/* ============================================================================
 * Block bitmap indexing
 *
 * The block bitmap for group g uses bit position p to represent disk block
 *
 *     g * blocks_per_group + s_first_data_block + p
 *
 * (s_first_data_block is 1 for 1 KiB-block filesystems, where block 0 is the
 * reserved boot block and is not represented in any bitmap, and 0 otherwise).
 * Equivalently, the bit for a block B in group g is:
 *
 *     B - g * blocks_per_group - s_first_data_block
 * ============================================================================ */

static inline uint32_t ext2_bit_of_block(ext2_fs_info_t *fs, uint32_t group,
                                         uint32_t block)
{
    return block - group * fs->blocks_per_group - fs->sb.s_first_data_block;
}

/* Disk block represented by bit p in group g's block bitmap. */
static inline uint32_t ext2_block_of_bit(ext2_fs_info_t *fs, uint32_t group,
                                         uint32_t p)
{
    return group * fs->blocks_per_group + fs->sb.s_first_data_block + p;
}

/**
 * Map a file block to a disk block.  A result of 0 means "sparse hole"
 * (block not allocated); reads return zeros, writes allocate.
 */
int ext2_bmap(ext2_fs_info_t *fs, struct ext2_inode *inode,
              uint32_t file_block, uint32_t *block_out)
{
    uint32_t ptrs = ext2_ptrs_per_block(fs);
    uint8_t buf[4096];

    /* Direct blocks */
    if (file_block < EXT2_NDIR_BLOCKS) {
        *block_out = inode->i_block[file_block];
        return 0;
    }
    file_block -= EXT2_NDIR_BLOCKS;

    /* Single indirect */
    if (file_block < ptrs) {
        if (inode->i_block[12] == 0) { *block_out = 0; return 0; }
        if (ext2_read_block(fs, inode->i_block[12], buf) < 0) return -1;
        *block_out = ((uint32_t *)buf)[file_block];
        return 0;
    }
    file_block -= ptrs;

    /* Double indirect */
    if (file_block < ptrs * ptrs) {
        if (inode->i_block[13] == 0) { *block_out = 0; return 0; }
        uint32_t idx1 = file_block / ptrs;
        uint32_t idx2 = file_block % ptrs;
        if (ext2_read_block(fs, inode->i_block[13], buf) < 0) return -1;
        uint32_t l1 = ((uint32_t *)buf)[idx1];
        if (l1 == 0) { *block_out = 0; return 0; }
        if (ext2_read_block(fs, l1, buf) < 0) return -1;
        *block_out = ((uint32_t *)buf)[idx2];
        return 0;
    }
    file_block -= ptrs * ptrs;

    /* Triple indirect */
    if (file_block < ptrs * ptrs * ptrs) {
        if (inode->i_block[14] == 0) { *block_out = 0; return 0; }
        uint32_t idx1 = file_block / (ptrs * ptrs);
        uint32_t rem   = file_block % (ptrs * ptrs);
        uint32_t idx2  = rem / ptrs;
        uint32_t idx3  = rem % ptrs;
        if (ext2_read_block(fs, inode->i_block[14], buf) < 0) return -1;
        uint32_t l1 = ((uint32_t *)buf)[idx1];
        if (l1 == 0) { *block_out = 0; return 0; }
        if (ext2_read_block(fs, l1, buf) < 0) return -1;
        uint32_t l2 = ((uint32_t *)buf)[idx2];
        if (l2 == 0) { *block_out = 0; return 0; }
        if (ext2_read_block(fs, l2, buf) < 0) return -1;
        *block_out = ((uint32_t *)buf)[idx3];
        return 0;
    }

    printk("[ext2] File block %u too large\n", file_block);
    return -1;
}

/* ============================================================================
 * Bitmap management
 *
 * Only one group's block+inode bitmaps are resident at a time.  Any changes
 * to the bitmaps also update that group's descriptor (free counts) and the
 * superblock free counts; ext2_sync_bitmaps() writes everything back.
 * ============================================================================ */

static int ext2_load_group_bitmaps(ext2_fs_info_t *fs, uint32_t group)
{
    if (group >= fs->num_groups) return -1;

    if (fs->bitmap_group == (int)group)
        return 0;   /* already resident */

    if (ext2_sync_bitmaps(fs) < 0)
        return -1;

    fs->bitmap_group = -1;

    uint32_t bb = fs->gdt[group].bg_block_bitmap;
    uint32_t ib = fs->gdt[group].bg_inode_bitmap;
    if (bb == 0 || ib == 0) {
        printk("[ext2] Group %u missing bitmaps (bb=%u ib=%u)\n", group, bb, ib);
        return -1;
    }
    if (ext2_read_block(fs, bb, fs->block_bitmap) < 0) return -1;
    if (ext2_read_block(fs, ib, fs->inode_bitmap) < 0) return -1;

    fs->block_bitmap_dirty = 0;
    fs->inode_bitmap_dirty = 0;
    fs->bitmap_group = (int)group;
    return 0;
}

int ext2_sync_bitmaps(ext2_fs_info_t *fs)
{
    if (fs->bitmap_group < 0)
        return 0;

    int group = fs->bitmap_group;

    if (fs->block_bitmap_dirty) {
        if (ext2_write_block(fs, fs->gdt[group].bg_block_bitmap,
                             fs->block_bitmap) < 0)
            return -1;
        fs->block_bitmap_dirty = 0;
    }
    if (fs->inode_bitmap_dirty) {
        if (ext2_write_block(fs, fs->gdt[group].bg_inode_bitmap,
                             fs->inode_bitmap) < 0)
            return -1;
        fs->inode_bitmap_dirty = 0;
    }

    /* Persist the updated group descriptors + superblock counts now so a
     * crash mid-operation cannot silently double-allocate. */
    if (fs->gdt_dirty || fs->sb_dirty) {
        uint8_t buf[4096];
        uint32_t gdt_block = fs->sb.s_first_data_block + 1;
        uint32_t gdt_bytes = fs->num_groups * sizeof(struct ext2_group_desc);
        uint32_t gdt_blocks = (gdt_bytes + fs->block_size - 1) / fs->block_size;
        for (uint32_t i = 0; i < gdt_blocks; i++) {
            uint32_t chunk = gdt_bytes - i * fs->block_size;
            if (chunk > fs->block_size) chunk = fs->block_size;
            if (ext2_read_block(fs, gdt_block + i, buf) < 0) return -1;
            memcpy(buf, (uint8_t *)fs->gdt + i * fs->block_size, chunk);
            if (ext2_write_block(fs, gdt_block + i, buf) < 0) return -1;
        }
        fs->gdt_dirty = 0;
    }
    if (fs->sb_dirty) {
        if (ext2_sync_super(fs) < 0) return -1;
        fs->sb_dirty = 0;
    }

    return 0;
}

/* ============================================================================
 * Block allocation
 * ============================================================================ */

/**
 * Allocate a free data block, preferring the group containing inode `ino`
 * (data locality for files) before falling back to any group with free
 * blocks.  The reserved block tail is never touched.
 */
int ext2_alloc_block(ext2_fs_info_t *fs, uint32_t ino, uint32_t *block_out)
{
    uint32_t ino_group = (ino >= 1) ? (ino - 1) / fs->inodes_per_group : 0;
    if (ino_group >= fs->num_groups) ino_group = 0;

    uint32_t limit = fs->sb.s_blocks_count - fs->sb.s_r_blocks_count;

    for (uint32_t g = 0; g < fs->num_groups; g++) {
        uint32_t group = (ino_group + g) % fs->num_groups;
        if (fs->gdt[group].bg_free_blocks_count == 0)
            continue;

        if (ext2_load_group_bitmaps(fs, group) < 0)
            return -1;

        /* First block with a valid bit in this group's bitmap. */
        uint32_t first = group * fs->blocks_per_group +
                         fs->sb.s_first_data_block;
        /* Never hand out blocks outside this group's physical range. */
        uint32_t last = (group + 1) * fs->blocks_per_group;
        if (last > limit)
            last = limit;

        for (uint32_t blk = first; blk < last; blk++) {
            uint32_t i = ext2_bit_of_block(fs, group, blk);
            if (i >= fs->blocks_per_group)
                break;
            if (!(fs->block_bitmap[i / 8] & (1 << (i % 8)))) {
                fs->block_bitmap[i / 8] |= (1 << (i % 8));
                fs->block_bitmap_dirty = 1;
                fs->gdt[group].bg_free_blocks_count--;
                fs->gdt_dirty = 1;
                fs->sb.s_free_blocks_count--;
                fs->sb_dirty = 1;
                *block_out = blk;
                return 0;
            }
        }
    }

    printk("[ext2] No free blocks\n");
    return -1;
}

/**
 * Free a data block (clear its bitmap bit and update the counters).
 */
int ext2_free_block(ext2_fs_info_t *fs, uint32_t block)
{
    if (block >= fs->sb.s_blocks_count) return -1;

    uint32_t group = block / fs->blocks_per_group;
    uint32_t index = ext2_bit_of_block(fs, group, block);
    if (group >= fs->num_groups || index >= fs->blocks_per_group)
        return -1;

    if (ext2_load_group_bitmaps(fs, group) < 0)
        return -1;

    if (!(fs->block_bitmap[index / 8] & (1 << (index % 8)))) {
        printk("[ext2] double-free of block %u\n", block);
        return -1;
    }

    fs->block_bitmap[index / 8] &= ~(1 << (index % 8));
    fs->block_bitmap_dirty = 1;
    fs->gdt[group].bg_free_blocks_count++;
    fs->gdt_dirty = 1;
    fs->sb.s_free_blocks_count++;
    fs->sb_dirty = 1;
    return 0;
}

/* ============================================================================
 * Inode allocation
 * ============================================================================ */

/**
 * Allocate a free inode, zero it and initialise mode/times.  `pref_group`
 * is the group we try first (the parent directory's group for mkdir).
 */
int ext2_alloc_inode(ext2_fs_info_t *fs, uint16_t mode, uint32_t pref_group,
                     uint32_t *ino_out)
{
    if (pref_group >= fs->num_groups) pref_group = 0;

    for (uint32_t g = 0; g < fs->num_groups; g++) {
        uint32_t group = (pref_group + g) % fs->num_groups;
        if (fs->gdt[group].bg_free_inodes_count == 0)
            continue;

        if (ext2_load_group_bitmaps(fs, group) < 0)
            return -1;

        for (uint32_t i = 0; i < fs->inodes_per_group; i++) {
            if (!(fs->inode_bitmap[i / 8] & (1 << (i % 8)))) {
                uint32_t ino = group * fs->inodes_per_group + i + 1;
                if (ino > fs->sb.s_inodes_count)
                    return -1;

                /* Claim the inode in the bitmap. */
                fs->inode_bitmap[i / 8] |= (1 << (i % 8));
                fs->inode_bitmap_dirty = 1;
                fs->gdt[group].bg_free_inodes_count--;
                if (EXT2_ISDIR(mode))
                    fs->gdt[group].bg_used_dirs_count++;
                fs->gdt_dirty = 1;
                fs->sb.s_free_inodes_count--;
                fs->sb_dirty = 1;

                /* Initialise the on-disk inode. */
                struct ext2_inode inode;
                memset(&inode, 0, sizeof(inode));
                inode.i_mode = mode;
                inode.i_uid = 0;
                inode.i_gid = 0;
                inode.i_links_count = 1;
                uint32_t now = rtc_get_unix_time();
                inode.i_atime = now;
                inode.i_ctime = now;
                inode.i_mtime = now;

                if (ext2_write_inode(fs, ino, &inode) < 0) {
                    /* Roll back the bitmap claim. */
                    fs->inode_bitmap[i / 8] &= ~(1 << (i % 8));
                    fs->inode_bitmap_dirty = 1;
                    fs->gdt[group].bg_free_inodes_count++;
                    if (EXT2_ISDIR(mode))
                        fs->gdt[group].bg_used_dirs_count--;
                    fs->gdt_dirty = 1;
                    fs->sb.s_free_inodes_count++;
                    fs->sb_dirty = 1;
                    return -1;
                }

                *ino_out = ino;
                return 0;
            }
        }
    }

    printk("[ext2] No free inodes\n");
    return -1;
}

/**
 * Free an inode: clear its bitmap bit, zero the on-disk inode, and record
 * the deletion time (required by e2fsck / ls-deleted).
 */
int ext2_free_inode(ext2_fs_info_t *fs, uint32_t ino)
{
    if (ino < 1 || ino > fs->sb.s_inodes_count) return -1;

    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;

    struct ext2_inode inode;
    if (ext2_read_inode(fs, ino, &inode) < 0) return -1;

    if (ext2_load_group_bitmaps(fs, group) < 0) return -1;

    if (!(fs->inode_bitmap[index / 8] & (1 << (index % 8)))) {
        printk("[ext2] double-free of inode %u\n", ino);
        return -1;
    }

    fs->inode_bitmap[index / 8] &= ~(1 << (index % 8));
    fs->inode_bitmap_dirty = 1;
    fs->gdt[group].bg_free_inodes_count++;
    if (EXT2_ISDIR(inode.i_mode))
        fs->gdt[group].bg_used_dirs_count--;
    fs->gdt_dirty = 1;
    fs->sb.s_free_inodes_count++;
    fs->sb_dirty = 1;

    memset(&inode, 0, sizeof(inode));
    inode.i_dtime = rtc_get_unix_time();
    return ext2_write_inode(fs, ino, &inode);
}

/* ============================================================================
 * Block mapping with allocation (write path)
 * ============================================================================ */

/* Allocate + zero a block and return it. */
static int ext2_alloc_zeroed_block(ext2_fs_info_t *fs, uint32_t ino,
                                   uint32_t *block_out)
{
    uint32_t blk;
    uint8_t buf[4096];
    if (ext2_alloc_block(fs, ino, &blk) < 0)
        return -1;
    memset(buf, 0, fs->block_size);
    if (ext2_write_block(fs, blk, buf) < 0) {
        ext2_free_block(fs, blk);
        return -1;
    }
    *block_out = blk;
    return 0;
}

/**
 * Map a file block to a disk block, allocating data + indirect blocks as
 * needed (sparse holes get filled).  The caller must write the inode back
 * afterwards if any i_block[] pointer changed.
 */
int ext2_bmap_alloc(ext2_fs_info_t *fs, uint32_t ino, struct ext2_inode *inode,
                    uint32_t file_block, uint32_t *block_out)
{
    uint32_t ptrs = ext2_ptrs_per_block(fs);
    uint8_t buf[4096];

    /* Direct blocks */
    if (file_block < EXT2_NDIR_BLOCKS) {
        if (inode->i_block[file_block] == 0) {
            uint32_t blk;
            if (ext2_alloc_zeroed_block(fs, ino, &blk) < 0) return -1;
            inode->i_block[file_block] = blk;
        }
        *block_out = inode->i_block[file_block];
        return 0;
    }
    file_block -= EXT2_NDIR_BLOCKS;

    /* Single indirect */
    if (file_block < ptrs) {
        if (inode->i_block[12] == 0) {
            uint32_t blk;
            if (ext2_alloc_zeroed_block(fs, ino, &blk) < 0) return -1;
            inode->i_block[12] = blk;
        }
        if (ext2_read_block(fs, inode->i_block[12], buf) < 0) return -1;
        uint32_t *p = (uint32_t *)buf;
        if (p[file_block] == 0) {
            uint32_t blk;
            if (ext2_alloc_zeroed_block(fs, ino, &blk) < 0) return -1;
            p[file_block] = blk;
            if (ext2_write_block(fs, inode->i_block[12], buf) < 0) return -1;
        }
        *block_out = p[file_block];
        return 0;
    }
    file_block -= ptrs;

    /* Double indirect */
    if (file_block < ptrs * ptrs) {
        if (inode->i_block[13] == 0) {
            uint32_t blk;
            if (ext2_alloc_zeroed_block(fs, ino, &blk) < 0) return -1;
            inode->i_block[13] = blk;
        }
        if (ext2_read_block(fs, inode->i_block[13], buf) < 0) return -1;
        uint32_t *p1 = (uint32_t *)buf;
        uint32_t idx1 = file_block / ptrs;
        uint32_t idx2 = file_block % ptrs;
        if (p1[idx1] == 0) {
            uint32_t blk;
            if (ext2_alloc_zeroed_block(fs, ino, &blk) < 0) return -1;
            p1[idx1] = blk;
            if (ext2_write_block(fs, inode->i_block[13], buf) < 0) return -1;
        }
        if (ext2_read_block(fs, p1[idx1], buf) < 0) return -1;
        uint32_t *p2 = (uint32_t *)buf;
        if (p2[idx2] == 0) {
            uint32_t blk;
            if (ext2_alloc_zeroed_block(fs, ino, &blk) < 0) return -1;
            p2[idx2] = blk;
            if (ext2_write_block(fs, p1[idx1], buf) < 0) return -1;
        }
        *block_out = p2[idx2];
        return 0;
    }
    file_block -= ptrs * ptrs;
    /* Triple indirect */
    if (file_block < ptrs * ptrs * ptrs) {
        if (inode->i_block[14] == 0) {
            uint32_t blk;
            if (ext2_alloc_zeroed_block(fs, ino, &blk) < 0) return -1;
            inode->i_block[14] = blk;
        }
        if (ext2_read_block(fs, inode->i_block[14], buf) < 0) return -1;
        uint32_t *p1 = (uint32_t *)buf;
        uint32_t idx1 = file_block / (ptrs * ptrs);
        uint32_t rem  = file_block % (ptrs * ptrs);
        uint32_t idx2 = rem / ptrs;
        uint32_t idx3 = rem % ptrs;
        if (p1[idx1] == 0) {
            uint32_t blk;
            if (ext2_alloc_zeroed_block(fs, ino, &blk) < 0) return -1;
            p1[idx1] = blk;
            if (ext2_write_block(fs, inode->i_block[14], buf) < 0) return -1;
        }
        if (ext2_read_block(fs, p1[idx1], buf) < 0) return -1;
        uint32_t *p2 = (uint32_t *)buf;
        if (p2[idx2] == 0) {
            uint32_t blk;
            if (ext2_alloc_zeroed_block(fs, ino, &blk) < 0) return -1;
            p2[idx2] = blk;
            if (ext2_write_block(fs, p1[idx1], buf) < 0) return -1;
        }
        if (ext2_read_block(fs, p2[idx2], buf) < 0) return -1;
        uint32_t *p3 = (uint32_t *)buf;
        if (p3[idx3] == 0) {
            uint32_t blk;
            if (ext2_alloc_zeroed_block(fs, ino, &blk) < 0) return -1;
            p3[idx3] = blk;
            if (ext2_write_block(fs, p2[idx2], buf) < 0) return -1;
        }
        *block_out = p3[idx3];
        return 0;
    }

    printk("[ext2] File block too large\n");
    return -1;
}

/* ============================================================================
 * Block counting
 *
 * Recompute i_blocks (in 512-byte units) by walking the full pointer tree.
 * Used after write/truncate so the on-disk block count stays accurate.
 * ============================================================================ */

static uint32_t ext2_count_blocks(ext2_fs_info_t *fs, struct ext2_inode *inode)
{
    uint32_t ptrs = ext2_ptrs_per_block(fs);
    uint32_t count = 0;
    uint8_t buf[4096];
    uint32_t i;

    for (i = 0; i < EXT2_NDIR_BLOCKS; i++)
        if (inode->i_block[i]) count++;

    if (inode->i_block[12]) {
        count++;
        if (ext2_read_block(fs, inode->i_block[12], buf) == 0) {
            uint32_t *p = (uint32_t *)buf;
            for (i = 0; i < ptrs; i++) if (p[i]) count++;
        }
    }
    if (inode->i_block[13]) {
        count++;
        if (ext2_read_block(fs, inode->i_block[13], buf) == 0) {
            uint32_t *p1 = (uint32_t *)buf;
            for (i = 0; i < ptrs; i++) {
                if (!p1[i]) continue;
                count++;
                if (ext2_read_block(fs, p1[i], buf) == 0) {
                    uint32_t *p2 = (uint32_t *)buf;
                    for (uint32_t j = 0; j < ptrs; j++) if (p2[j]) count++;
                }
            }
        }
    }
    if (inode->i_block[14]) {
        count++;
        if (ext2_read_block(fs, inode->i_block[14], buf) == 0) {
            uint32_t *p1 = (uint32_t *)buf;
            for (i = 0; i < ptrs; i++) {
                if (!p1[i]) continue;
                count++;
                if (ext2_read_block(fs, p1[i], buf) == 0) {
                    uint32_t *p2 = (uint32_t *)buf;
                    for (uint32_t j = 0; j < ptrs; j++) {
                        if (!p2[j]) continue;
                        count++;
                        if (ext2_read_block(fs, p2[j], buf) == 0) {
                            uint32_t *p3 = (uint32_t *)buf;
                            for (uint32_t k = 0; k < ptrs; k++)
                                if (p3[k]) count++;
                        }
                    }
                }
            }
        }
    }

    return count * (fs->block_size / 512);
}

/* Update i_blocks from the actual pointer tree. */
void ext2_refresh_blocks(ext2_fs_info_t *fs, struct ext2_inode *inode)
{
    inode->i_blocks = ext2_count_blocks(fs, inode);
}

/* ============================================================================
 * Block range freeing (truncate)
 * ============================================================================ */

/**
 * Recursively free data blocks in file-block range [start, end) from the
 * subtree rooted at *slot (level 1: *slot is a data block; level > 1: *slot
 * is an indirect block of pointers at level-1).  Freed pointers are zeroed
 * and indirect blocks that become empty are freed too.  `start`/`end` are
 * relative to the subtree.
 */
static int ext2_free_range(ext2_fs_info_t *fs, uint32_t *slot, int level,
                           uint32_t start, uint32_t end)
{
    uint32_t ptrs = fs->block_size / 4;
    uint8_t buf[4096];
    uint32_t blk = *slot;

    if (blk == 0 || start >= end)
        return 0;

    if (level == 1) {
        /* Data blocks are always freed whole (truncation keeps a partial
         * tail block).  Only free when the range covers the entire block. */
        if (start == 0) {
            if (ext2_free_block(fs, blk) < 0)
                return -1;
            *slot = 0;
        }
        return 0;
    }

    if (ext2_read_block(fs, blk, buf) < 0)
        return -1;

    uint32_t *p = (uint32_t *)buf;
    /* Number of file blocks covered by one child entry.
     * level 2 (single indirect): each entry is one data block  -> 1
     * level 3 (double):          each entry covers ptrs blocks
     * level 4 (triple):          each entry covers ptrs^2 blocks */
    uint32_t per_child = 1;
    for (int l = 2; l < level; l++)
        per_child *= ptrs;

    int writeback = 0;
    int any_left = 0;
    for (uint32_t i = 0; i < ptrs; i++) {
        if (p[i] != 0) {
            uint32_t child_start = i * per_child;
            uint32_t child_end = child_start + per_child;
            if (child_end > start && child_start < end) {
                uint32_t cs = (start > child_start) ? start - child_start : 0;
                uint32_t ce = (end > child_end) ? per_child : end - child_start;
                if (ext2_free_range(fs, &p[i], level - 1, cs, ce) < 0)
                    return -1;
                writeback = 1;
            }
        }
        if (p[i] != 0)
            any_left = 1;
    }

    if (writeback) {
        if (ext2_write_block(fs, blk, buf) < 0)
            return -1;
    }
    if (!any_left) {
        if (ext2_free_block(fs, blk) < 0)
            return -1;
        *slot = 0;
    }
    return 0;
}

/**
 * Free data blocks for file blocks [new_blocks, old_blocks), across all
 * indirect levels, plus any indirect blocks that become empty.
 * Called by ext2_truncate_file.
 */
int ext2_truncate_blocks(ext2_fs_info_t *fs, struct ext2_inode *inode,
                         uint32_t new_blocks, uint32_t old_blocks)
{
    uint32_t i;
    uint32_t ptrs = fs->block_size / 4;
    uint32_t slot;

    if (new_blocks >= old_blocks)
        return 0;

    /* Direct blocks */
    for (i = new_blocks; i < old_blocks && i < EXT2_NDIR_BLOCKS; i++) {
        slot = inode->i_block[i];
        if (ext2_free_range(fs, &slot, 1, 0, 1) < 0)
            return -1;
        inode->i_block[i] = slot;
    }

    /* Single indirect (level 2: array of data-block pointers) */
    if (inode->i_block[12] && old_blocks > EXT2_NDIR_BLOCKS) {
        uint32_t s = (new_blocks > EXT2_NDIR_BLOCKS)
                     ? new_blocks - EXT2_NDIR_BLOCKS : 0;
        slot = inode->i_block[12];
        if (ext2_free_range(fs, &slot, 2, s,
                            old_blocks - EXT2_NDIR_BLOCKS) < 0)
            return -1;
        inode->i_block[12] = slot;
    }

    /* Double indirect (level 3) */
    if (inode->i_block[13] && old_blocks > EXT2_NDIR_BLOCKS + ptrs) {
        uint32_t base = EXT2_NDIR_BLOCKS + ptrs;
        uint32_t s = (new_blocks > base) ? new_blocks - base : 0;
        slot = inode->i_block[13];
        if (ext2_free_range(fs, &slot, 3, s, old_blocks - base) < 0)
            return -1;
        inode->i_block[13] = slot;
    }

    /* Triple indirect (level 4) */
    if (inode->i_block[14] && old_blocks > EXT2_NDIR_BLOCKS + ptrs + ptrs * ptrs) {
        uint32_t base = EXT2_NDIR_BLOCKS + ptrs + ptrs * ptrs;
        uint32_t s = (new_blocks > base) ? new_blocks - base : 0;
        slot = inode->i_block[14];
        if (ext2_free_range(fs, &slot, 4, s, old_blocks - base) < 0)
            return -1;
        inode->i_block[14] = slot;
    }

    return 0;
}

