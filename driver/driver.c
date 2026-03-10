#include "driver/driver.h"
#include "driver/block/cache.h"
#include "mm/slab.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Device Structure (Internal)
 * ========================================================================= */

typedef struct device {
    int prim_id;
    dev_type_t type;
    union {
        char_ops_t char_ops;
        block_ops_t block_ops;
    } ops;
    struct device *next;
} device_t;

/* =========================================================================
 * Global State
 * ========================================================================= */

static device_t *char_device_list = NULL;
static device_t *block_device_list = NULL;

/* =========================================================================
 * Internal Helper Functions
 * ========================================================================= */

/* Find a character device by primary ID */
static device_t *find_char_device(int prim_id)
{
    device_t *dev = char_device_list;
    while (dev) {
        if (dev->prim_id == prim_id) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

/* Find a block device by primary ID */
static device_t *find_block_device(int prim_id)
{
    device_t *dev = block_device_list;
    while (dev) {
        if (dev->prim_id == prim_id) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

/* =========================================================================
 * Public API - Initialization
 * ========================================================================= */

void driver_init(void)
{
    char_device_list = NULL;
    block_device_list = NULL;
}

/* =========================================================================
 * Public API - Device Registration
 * ========================================================================= */

int register_char_device(int prim_id, char_ops_t *ops)
{
    if (!ops || prim_id < 0 || prim_id > 255) {
        return -1;
    }
    
    /* Check if device already registered */
    if (find_char_device(prim_id)) {
        return -1;
    }
    
    /* Allocate new device structure */
    device_t *dev = (device_t *)kalloc(sizeof(device_t));
    if (!dev) {
        return -1;
    }
    
    /* Initialize device */
    dev->prim_id = prim_id;
    dev->type = DEV_CHAR;
    dev->ops.char_ops = *ops;
    
    /* Add to front of char device list */
    dev->next = char_device_list;
    char_device_list = dev;
    
    return 0;
}

int register_block_device(int prim_id, block_ops_t *ops)
{
    if (!ops || prim_id < 0 || prim_id > 255) {
        return -1;
    }
    
    /* Check if device already registered */
    if (find_block_device(prim_id)) {
        return -1;
    }
    
    /* Allocate new device structure */
    device_t *dev = (device_t *)kalloc(sizeof(device_t));
    if (!dev) {
        return -1;
    }
    
    /* Initialize device */
    dev->prim_id = prim_id;
    dev->type = DEV_BLOCK;
    dev->ops.block_ops = *ops;
    
    /* Add to front of block device list */
    dev->next = block_device_list;
    block_device_list = dev;
    
    return 0;
}

/* =========================================================================
 * Public API - Character Device Operations
 * ========================================================================= */

char cread(int prim_id, int scnd_id)
{
    device_t *dev = find_char_device(prim_id);
    if (!dev) {
        return 0;
    }
    
    if (!dev->ops.char_ops.read) {
        return 0;
    }
    
    return dev->ops.char_ops.read(scnd_id);
}

int cwrite(int prim_id, int scnd_id, char c)
{
    device_t *dev = find_char_device(prim_id);
    if (!dev) {
        return -1;
    }
    
    if (!dev->ops.char_ops.write) {
        return -1;
    }
    
    return dev->ops.char_ops.write(scnd_id, c);
}

/* =========================================================================
 * Public API - Block Device Operations
 * ========================================================================= */

int bread(int prim_id, int scnd_id, void *buf, uint32_t offset, size_t count)
{
    device_t *dev = find_block_device(prim_id);
    if (!dev) {
        return -1;
    }
    
    if (!dev->ops.block_ops.read) {
        return -1;
    }
    
    /* Try cache first (only for single block reads at offset 0) */
    if (count == 1 && offset == 0) {
        if (cache_lookup(prim_id, scnd_id, 0, buf)) {
            /* Cache hit! Return immediately */
            return CACHE_BLOCK_SIZE;
        }
    }
    
    /* Cache miss or multi-block/offset read - read from device */
    int ret = dev->ops.block_ops.read(prim_id, scnd_id, buf, offset, count);
    
    /* Cache the block if single block at offset 0 and read succeeded */
    if (ret > 0 && count == 1 && offset == 0) {
        cache_insert(prim_id, scnd_id, 0, buf);
    }
    
    return ret;
}

int bwrite(int prim_id, int scnd_id, const void *buf, uint32_t offset, size_t count)
{
    device_t *dev = find_block_device(prim_id);
    if (!dev) {
        return -1;
    }
    
    if (!dev->ops.block_ops.write) {
        return -1;
    }
    
    /* Write to device */
    int ret = dev->ops.block_ops.write(prim_id, scnd_id, buf, offset, count);
    
    /* Update cache if write succeeded and single block at offset 0 */
    if (ret > 0 && count == 1 && offset == 0) {
        /* Insert/update in cache and mark as clean (write-through) */
        cache_insert(prim_id, scnd_id, 0, buf);
    }
    
    return ret;
}

/* =========================================================================
 * Public API - ioctl Operations (Unified for Char and Block)
 * ========================================================================= */

int ioctl(int prim_id, int scnd_id, unsigned int command)
{
    /* Try character device first */
    device_t *dev = find_char_device(prim_id);
    if (dev) {
        if (!dev->ops.char_ops.ioctl) {
            return -1;  /* Device doesn't support ioctl */
        }
        return dev->ops.char_ops.ioctl(prim_id, scnd_id, command);
    }
    
    /* Try block device */
    dev = find_block_device(prim_id);
    if (dev) {
        if (!dev->ops.block_ops.ioctl) {
            return -1;  /* Device doesn't support ioctl */
        }
        return dev->ops.block_ops.ioctl(prim_id, scnd_id, command);
    }
    
    /* Device not found */
    return -1;
}
