#ifndef DRIVER_BLOCK_H
#define DRIVER_BLOCK_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Shared ioctl callback type (guarded, same definition as in char.h)
 * ========================================================================= */
#ifndef IOCTL_FN_T
#define IOCTL_FN_T
typedef int (*ioctl_fn)(int prim_id, int scnd_id, unsigned int command);
#endif

/* =========================================================================
 * Block device callback types
 * ========================================================================= */
typedef int (*block_read_fn)(int prim_id, int scnd_id, void *buf,
                             uint32_t offset, size_t count);
typedef int (*block_write_fn)(int prim_id, int scnd_id, const void *buf,
                              uint32_t offset, size_t count);

typedef struct {
    block_read_fn  read;
    block_write_fn write;
    ioctl_fn       ioctl;
} block_ops_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/** Register a block device (prim_id 0-255). Returns 0 or -1. */
int register_block_device(int prim_id, block_ops_t *ops);

/**
 * Read count blocks starting at offset from block device prim_id.
 * Goes through the LRU cache for single-block reads at offset 0.
 * Returns bytes read or -1 on error.
 */
int bread(int prim_id, int scnd_id, void *buf, uint32_t offset, size_t count);

/**
 * Write count blocks starting at offset to block device prim_id.
 * Keeps cache consistent on single-block writes at offset 0.
 * Returns bytes written or -1 on error.
 */
int bwrite(int prim_id, int scnd_id, const void *buf,
           uint32_t offset, size_t count);

/** Send an ioctl command to a block device. Returns device value or -1. */
int block_ioctl(int prim_id, int scnd_id, unsigned int command);

/**
 * Initialise all block devices: IDE disks, MBR partition table.
 * Must be called after cache_init().
 */
void block_init(void);

#endif /* DRIVER_BLOCK_H */