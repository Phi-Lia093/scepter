#include "driver/driver.h"
#include "driver/block/cache.h"
#include "mm/slab.h"
#include "lib/list.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Device Structure (Internal)
 * ========================================================================= */

typedef struct device {
    int prim_id;
    dev_type_t type;
    union {
        char_ops_t  char_ops;
        block_ops_t block_ops;
    } ops;
    list_head_t node;   /* embedded in char_device_list or block_device_list */
} device_t;

/* =========================================================================
 * Global Device Lists
 * ========================================================================= */

static LIST_HEAD(char_device_list);
static LIST_HEAD(block_device_list);

/* =========================================================================
 * Internal Helper Functions
 * ========================================================================= */

static device_t *find_char_device(int prim_id)
{
    device_t *dev;
    list_for_each_entry(dev, &char_device_list, node) {
        if (dev->prim_id == prim_id)
            return dev;
    }
    return NULL;
}

static device_t *find_block_device(int prim_id)
{
    device_t *dev;
    list_for_each_entry(dev, &block_device_list, node) {
        if (dev->prim_id == prim_id)
            return dev;
    }
    return NULL;
}

/* =========================================================================
 * Public API - Initialization
 * ========================================================================= */

void driver_init(void)
{
    INIT_LIST_HEAD(&char_device_list);
    INIT_LIST_HEAD(&block_device_list);
}

/* =========================================================================
 * Public API - Device Registration
 * ========================================================================= */

int register_char_device(int prim_id, char_ops_t *ops)
{
    if (!ops || prim_id < 0 || prim_id > 255)
        return -1;

    if (find_char_device(prim_id))
        return -1;

    device_t *dev = (device_t *)kalloc(sizeof(device_t));
    if (!dev)
        return -1;

    dev->prim_id       = prim_id;
    dev->type          = DEV_CHAR;
    dev->ops.char_ops  = *ops;

    list_add(&dev->node, &char_device_list);
    return 0;
}

int register_block_device(int prim_id, block_ops_t *ops)
{
    if (!ops || prim_id < 0 || prim_id > 255)
        return -1;

    if (find_block_device(prim_id))
        return -1;

    device_t *dev = (device_t *)kalloc(sizeof(device_t));
    if (!dev)
        return -1;

    dev->prim_id        = prim_id;
    dev->type           = DEV_BLOCK;
    dev->ops.block_ops  = *ops;

    list_add(&dev->node, &block_device_list);
    return 0;
}

/* =========================================================================
 * Public API - Character Device Operations
 * ========================================================================= */

char cread(int prim_id, int scnd_id)
{
    device_t *dev = find_char_device(prim_id);
    if (!dev || !dev->ops.char_ops.read)
        return 0;
    return dev->ops.char_ops.read(scnd_id);
}

int cwrite(int prim_id, int scnd_id, char c)
{
    device_t *dev = find_char_device(prim_id);
    if (!dev || !dev->ops.char_ops.write)
        return -1;
    return dev->ops.char_ops.write(scnd_id, c);
}

/* =========================================================================
 * Public API - Block Device Operations
 * ========================================================================= */

int bread(int prim_id, int scnd_id, void *buf, uint32_t offset, size_t count)
{
    device_t *dev = find_block_device(prim_id);
    if (!dev || !dev->ops.block_ops.read)
        return -1;

    /* Try cache first (only for single-block reads at offset 0) */
    if (count == 1 && offset == 0) {
        if (cache_lookup(prim_id, scnd_id, 0, buf))
            return CACHE_BLOCK_SIZE;
    }

    int ret = dev->ops.block_ops.read(prim_id, scnd_id, buf, offset, count);

    if (ret > 0 && count == 1 && offset == 0)
        cache_insert(prim_id, scnd_id, 0, buf);

    return ret;
}

int bwrite(int prim_id, int scnd_id, const void *buf, uint32_t offset, size_t count)
{
    device_t *dev = find_block_device(prim_id);
    if (!dev || !dev->ops.block_ops.write)
        return -1;

    int ret = dev->ops.block_ops.write(prim_id, scnd_id, buf, offset, count);

    if (ret > 0 && count == 1 && offset == 0)
        cache_insert(prim_id, scnd_id, 0, buf);

    return ret;
}

/* =========================================================================
 * Public API - ioctl (unified for char and block)
 * ========================================================================= */

int ioctl(int prim_id, int scnd_id, unsigned int command)
{
    device_t *dev = find_char_device(prim_id);
    if (dev) {
        if (!dev->ops.char_ops.ioctl)
            return -1;
        return dev->ops.char_ops.ioctl(prim_id, scnd_id, command);
    }

    dev = find_block_device(prim_id);
    if (dev) {
        if (!dev->ops.block_ops.ioctl)
            return -1;
        return dev->ops.block_ops.ioctl(prim_id, scnd_id, command);
    }

    return -1;
}