/* ============================================================================
 * ext2 Filesystem - Superblock Operations
 * ============================================================================ */

#include "fs/ext2.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "driver/char/rtc.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "lib/string.h"

/* -------------------------------------------------------------------------
 * Feature gate
 *
 * We mount only filesystems whose feature set we fully understand.  Anything
 * that could silently corrupt data (ext3/4 journals, extents, 64-bit block
 * numbers, flex_bg, metadata checksums, ...) is refused with a clear error.
 * ------------------------------------------------------------------------- */

/* Incompatible features we know how to handle. */
#define EXT2_SUPPORTED_INCOMPAT (EXT2_FEATURE_INCOMPAT_FILETYPE)

/* Read-only-compatible features that are safe to ignore. */
#define EXT2_SUPPORTED_RO_COMPAT \
    (EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER | EXT2_FEATURE_RO_COMPAT_LARGE_FILE | \
     EXT2_FEATURE_RO_COMPAT_DIR_NLINK    | EXT2_FEATURE_RO_COMPAT_EXTRA_ISIZE)

/* Compatible features that are safe to ignore.  HAS_JOURNAL (ext3) is
 * deliberately excluded: without replaying the journal the filesystem is
 * inconsistent, so we refuse to touch it. */
#define EXT2_SUPPORTED_COMPAT \
    (EXT2_FEATURE_COMPAT_DIR_PREALLOC  | EXT2_FEATURE_COMPAT_IMAGIC_INODES | \
     EXT2_FEATURE_COMPAT_EXT_ATTR      | EXT2_FEATURE_COMPAT_RESIZE_INODE  | \
     EXT2_FEATURE_COMPAT_DIR_INDEX     | EXT2_FEATURE_COMPAT_LAZY_BG)

/* ============================================================================
 * Low-level block helpers
 * ============================================================================ */

/* Read one filesystem block (block_number * block_size) into buf. */
int ext2_read_block(ext2_fs_info_t *fs, uint32_t block, void *buf)
{
    if (block >= fs->sb.s_blocks_count) {
        printk("[ext2] read_block: block %u out of range (%u)\n",
               block, fs->sb.s_blocks_count);
        return -1;
    }
    uint32_t sector = block * (fs->block_size / 512);
    int sectors = fs->block_size / 512;
    return bread(fs->device_id, fs->partition_id, buf, sector, sectors);
}

/* Write one filesystem block. */
int ext2_write_block(ext2_fs_info_t *fs, uint32_t block, const void *buf)
{
    if (block >= fs->sb.s_blocks_count) {
        printk("[ext2] write_block: block %u out of range (%u)\n",
               block, fs->sb.s_blocks_count);
        return -1;
    }
    uint32_t sector = block * (fs->block_size / 512);
    int sectors = fs->block_size / 512;
    return bwrite(fs->device_id, fs->partition_id, buf, sector, sectors);
}

/* ============================================================================
 * Superblock load / save
 * ============================================================================ */

/* Read the 1024-byte superblock (byte offset 1024 = sectors 2..3). */
static int ext2_read_super(ext2_fs_info_t *fs, struct ext2_super_block *sb)
{
    uint8_t buf[1024];
    if (bread(fs->device_id, fs->partition_id, buf, 2, 2) < 0) {
        printk("[ext2] Failed to read superblock\n");
        return -1;
    }
    memcpy(sb, buf, sizeof(struct ext2_super_block));
    return 0;
}

/* Write the superblock back to byte offset 1024. */
static int ext2_write_super(ext2_fs_info_t *fs)
{
    uint8_t buf[1024];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, &fs->sb, sizeof(struct ext2_super_block));
    if (bwrite(fs->device_id, fs->partition_id, buf, 2, 2) < 0) {
        printk("[ext2] Failed to write superblock\n");
        return -1;
    }
    return 0;
}

int ext2_sync_super(ext2_fs_info_t *fs)
{
    return ext2_write_super(fs);
}

/* ============================================================================
 * Mount
 * ============================================================================ */

/**
 * Mount an ext2 filesystem.
 * @param dev_id     Block device id (prim id)
 * @param part_id    Partition id (scnd id)
 * @param fs_private Output: allocated ext2_fs_info_t
 * @return 0 on success, -1 on error
 */
int ext2_mount(int dev_id, int part_id, void **fs_private)
{
    if (!fs_private) return -1;

    ext2_fs_info_t *fs = (ext2_fs_info_t *)kalloc(sizeof(ext2_fs_info_t));
    if (!fs) {
        printk("[ext2] Failed to allocate fs_info\n");
        return -1;
    }
    memset(fs, 0, sizeof(*fs));
    fs->device_id = dev_id;
    fs->partition_id = part_id;
    fs->bitmap_group = -1;

    /* ---- Superblock ---- */
    if (ext2_read_super(fs, &fs->sb) < 0) {
        kfree(fs);
        return -1;
    }
    if (fs->sb.s_magic != EXT2_SUPER_MAGIC) {
        printk("[ext2] Invalid magic 0x%04x (expected 0xEF53)\n", fs->sb.s_magic);
        kfree(fs);
        return -1;
    }

    /* ---- Block size ---- */
    if (fs->sb.s_log_block_size > 2) {
        printk("[ext2] Unsupported block size (log=%u)\n", fs->sb.s_log_block_size);
        kfree(fs);
        return -1;
    }
    fs->block_size = 1024 << fs->sb.s_log_block_size;

    /* ---- Inode size ---- */
    if (fs->sb.s_rev_level == EXT2_GOOD_OLD_REV) {
        fs->inode_size = EXT2_GOOD_OLD_INODE_SIZE;
        if (fs->sb.s_first_ino == 0)
            fs->sb.s_first_ino = EXT2_GOOD_OLD_FIRST_INO;
    } else {
        fs->inode_size = fs->sb.s_inode_size;
    }
    if (fs->inode_size < EXT2_GOOD_OLD_INODE_SIZE || fs->inode_size > 512 ||
        (fs->inode_size % 4) != 0) {
        printk("[ext2] Unsupported inode size %u\n", fs->inode_size);
        kfree(fs);
        return -1;
    }

    /* ---- Feature gate ---- */
    uint32_t incompat  = fs->sb.s_feature_incompat;
    uint32_t ro_compat = fs->sb.s_feature_ro_compat;
    uint32_t compat    = fs->sb.s_feature_compat;

    if (incompat & ~EXT2_SUPPORTED_INCOMPAT) {
        printk("[ext2] Refusing mount: unsupported incompatible features 0x%08x\n",
               incompat);
        kfree(fs);
        return -1;
    }
    if (ro_compat & ~EXT2_SUPPORTED_RO_COMPAT) {
        printk("[ext2] Refusing mount: unsupported ro-compat features 0x%08x\n",
               ro_compat);
        kfree(fs);
        return -1;
    }
    if (compat & EXT2_FEATURE_COMPAT_HAS_JOURNAL) {
        printk("[ext2] Refusing mount: filesystem has a journal (ext3/4)\n");
        kfree(fs);
        return -1;
    }

    /* ---- Geometry ---- */
    fs->blocks_per_group = fs->sb.s_blocks_per_group;
    fs->inodes_per_group = fs->sb.s_inodes_per_group;
    if (fs->blocks_per_group == 0 || fs->inodes_per_group == 0) {
        printk("[ext2] Corrupt geometry (bpg=%u ipg=%u)\n",
               fs->blocks_per_group, fs->inodes_per_group);
        kfree(fs);
        return -1;
    }
    fs->blocks_per_group_mask = fs->blocks_per_group - 1;
    fs->num_groups = (fs->sb.s_blocks_count + fs->blocks_per_group - 1) /
                     fs->blocks_per_group;
    if (fs->num_groups == 0 ||
        fs->num_groups > (fs->sb.s_blocks_count / 8)) {
        printk("[ext2] Corrupt group count %u\n", fs->num_groups);
        kfree(fs);
        return -1;
    }

    /* ---- Group descriptor table ---- */
    fs->gdt = (struct ext2_group_desc *)
              kalloc(fs->num_groups * sizeof(struct ext2_group_desc));
    if (!fs->gdt) {
        printk("[ext2] Failed to allocate group descriptor table\n");
        kfree(fs);
        return -1;
    }
    memset(fs->gdt, 0, fs->num_groups * sizeof(struct ext2_group_desc));

    /* GDT starts at the block right after the superblock. */
    uint32_t gdt_block = fs->sb.s_first_data_block + 1;
    uint32_t gdt_bytes = fs->num_groups * sizeof(struct ext2_group_desc);
    uint32_t gdt_blocks = (gdt_bytes + fs->block_size - 1) / fs->block_size;

    for (uint32_t i = 0; i < gdt_blocks; i++) {
        uint8_t buf[4096];
        if (ext2_read_block(fs, gdt_block + i, buf) < 0) {
            kfree(fs->gdt);
            kfree(fs);
            return -1;
        }
        uint32_t chunk = fs->num_groups * sizeof(struct ext2_group_desc) -
                         i * fs->block_size;
        if (chunk > fs->block_size) chunk = fs->block_size;
        memcpy((uint8_t *)fs->gdt + i * fs->block_size, buf, chunk);
    }

    /* ---- Bitmap buffers ---- */
    fs->block_bitmap = (uint8_t *)kalloc(fs->block_size);
    fs->inode_bitmap = (uint8_t *)kalloc(fs->block_size);
    if (!fs->block_bitmap || !fs->inode_bitmap) {
        if (fs->block_bitmap) kfree(fs->block_bitmap);
        if (fs->inode_bitmap) kfree(fs->inode_bitmap);
        kfree(fs->gdt);
        kfree(fs);
        return -1;
    }

    /* ---- Mark the filesystem dirty (we may write to it) ---- */
    if (fs->sb.s_state == EXT2_VALID_FS) {
        fs->sb.s_state = 0;      /* clear EXT2_VALID_FS: dirty */
        fs->sb.s_mnt_count++;
        fs->sb.s_mtime = rtc_get_unix_time();
        if (ext2_write_super(fs) < 0) {
            kfree(fs->block_bitmap);
            kfree(fs->inode_bitmap);
            kfree(fs->gdt);
            kfree(fs);
            return -1;
        }
    }

    printk("[ext2] %u/%u blocks free, %u/%u inodes free, block size %u, "
           "%u group(s), inode size %u\n",
           fs->sb.s_free_blocks_count, fs->sb.s_blocks_count,
           fs->sb.s_free_inodes_count, fs->sb.s_inodes_count,
           fs->block_size, fs->num_groups, fs->inode_size);

    *fs_private = fs;
    return 0;
}

/* ============================================================================
 * Unmount
 * ============================================================================ */

int ext2_unmount(void *fs_private)
{
    if (!fs_private) return -1;
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;

    /* Flush cached bitmaps and their group descriptor updates. */
    ext2_sync_bitmaps(fs);

    /* If we cleared the dir_index feature, persist it. */
    if (fs->dir_index_pending) {
        fs->sb.s_feature_compat &= ~EXT2_FEATURE_COMPAT_DIR_INDEX;
        fs->dir_index_pending = 0;
    }

    /* Mark the filesystem clean. */
    fs->sb.s_state = EXT2_VALID_FS;
    fs->sb.s_wtime = rtc_get_unix_time();
    ext2_write_super(fs);

    kfree(fs->block_bitmap);
    kfree(fs->inode_bitmap);
    kfree(fs->gdt);
    kfree(fs);

    printk("[ext2] Unmounted\n");
    return 0;
}

