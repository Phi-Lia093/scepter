#include "driver/block/block.h"
#include "driver/block/cache.h"
#include "driver/block/ide.h"
#include "driver/block/part_mbr.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Block device registry
 *
 * Uses a static fixed-size array so registration is allocation-free.
 * ========================================================================= */

#define MAX_BLOCK_DEVICES 16

typedef struct {
    int         prim_id;
    block_ops_t ops;
    int         in_use;
} block_device_t;

static block_device_t block_devices[MAX_BLOCK_DEVICES];

/* -------------------------------------------------------------------------
 * Internal lookup
 * ------------------------------------------------------------------------- */

static block_device_t *find_block_device(int prim_id)
{
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (block_devices[i].in_use && block_devices[i].prim_id == prim_id)
            return &block_devices[i];
    }
    return NULL;
}

/* =========================================================================
 * Public API – registration
 * ========================================================================= */

int register_block_device(int prim_id, block_ops_t *ops)
{
    if (!ops || prim_id < 0 || prim_id > 255)
        return -1;

    if (find_block_device(prim_id))
        return -1;   /* already registered */

    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (!block_devices[i].in_use) {
            block_devices[i].prim_id = prim_id;
            block_devices[i].ops    = *ops;
            block_devices[i].in_use = 1;
            return 0;
        }
    }
    return -1;   /* table full */
}

/* =========================================================================
 * Public API – I/O (with LRU cache)
 * ========================================================================= */

int bread(int prim_id, int scnd_id, void *buf, uint32_t offset, size_t count)
{
    block_device_t *dev = find_block_device(prim_id);
    if (!dev || !dev->ops.read)
        return -1;

    /* Try cache first for single-block reads at offset 0 */
    if (count == 1 && offset == 0) {
        if (cache_lookup(prim_id, scnd_id, 0, buf))
            return CACHE_BLOCK_SIZE;
    }

    int ret = dev->ops.read(prim_id, scnd_id, buf, offset, count);

    if (ret > 0 && count == 1 && offset == 0)
        cache_insert(prim_id, scnd_id, 0, buf);

    return ret;
}

int bwrite(int prim_id, int scnd_id, const void *buf,
           uint32_t offset, size_t count)
{
    block_device_t *dev = find_block_device(prim_id);
    if (!dev || !dev->ops.write)
        return -1;

    int ret = dev->ops.write(prim_id, scnd_id, buf, offset, count);

    /* Write-through: update cache on single-block writes at offset 0 */
    if (ret > 0 && count == 1 && offset == 0)
        cache_insert(prim_id, scnd_id, 0, buf);

    return ret;
}

int block_ioctl(int prim_id, int scnd_id, unsigned int command)
{
    block_device_t *dev = find_block_device(prim_id);
    if (!dev || !dev->ops.ioctl)
        return -1;
    return dev->ops.ioctl(prim_id, scnd_id, command);
}

/* =========================================================================
 * Aggregator – initialise all block devices
 *
 * ide_init() self-registers IDE disks as block devices and populates
 * devfs nodes.  mbr_init() parses MBR partition tables and registers
 * partition block devices; mbr_print_partitions() reports the result.
 * ========================================================================= */

void block_init(void)
{
    ide_init();             /* detect, register, devfs: hda-hdd */
    mbr_init();             /* parse MBR, register partition devs */
    mbr_print_partitions(); /* print partition table summary      */
}