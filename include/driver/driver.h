#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Device Driver Abstraction Layer
 *
 * Provides a unified interface for character and block devices.
 * Uses callbacks and linked list registration with kalloc.
 * ========================================================================= */

/* Device types */
typedef enum {
    DEV_CHAR = 0,
    DEV_BLOCK = 1
} dev_type_t;

/* -------------------------------------------------------------------------
 * Callback Function Types
 * ------------------------------------------------------------------------- */

/* Character device callbacks */
typedef char (*char_read_fn)(int scnd_id);
typedef int (*char_write_fn)(int scnd_id, char c);

/* Block device callbacks */
typedef int (*block_read_fn)(int prim_id, int scnd_id, void *buf, uint32_t offset, size_t count);
typedef int (*block_write_fn)(int prim_id, int scnd_id, const void *buf, uint32_t offset, size_t count);

/* ioctl callback - unified for both char and block devices */
typedef int (*ioctl_fn)(int prim_id, int scnd_id, unsigned int command);

/* -------------------------------------------------------------------------
 * Device Operations Structures
 * ------------------------------------------------------------------------- */

typedef struct {
    char_read_fn read;
    char_write_fn write;
    ioctl_fn ioctl;
} char_ops_t;

typedef struct {
    block_read_fn read;
    block_write_fn write;
    ioctl_fn ioctl;
} block_ops_t;

/* -------------------------------------------------------------------------
 * Device Registration (Driver-End API)
 * ------------------------------------------------------------------------- */

/**
 * Register a character device driver
 * 
 * @param prim_id Primary device ID (0-255)
 * @param ops Pointer to operations structure
 * @return 0 on success, -1 on failure
 */
int register_char_device(int prim_id, char_ops_t *ops);

/**
 * Register a block device driver
 * 
 * @param prim_id Primary device ID (0-255)
 * @param ops Pointer to operations structure
 * @return 0 on success, -1 on failure
 */
int register_block_device(int prim_id, block_ops_t *ops);

/* -------------------------------------------------------------------------
 * Device Access (User-End API)
 * ------------------------------------------------------------------------- */

/**
 * Read a character from a character device
 * 
 * @param prim_id Primary device ID
 * @param scnd_id Secondary device ID (for device variants)
 * @return Character read, or 0 on error
 */
char cread(int prim_id, int scnd_id);

/**
 * Write a character to a character device
 * 
 * @param prim_id Primary device ID
 * @param scnd_id Secondary device ID
 * @param c Character to write
 * @return 0 on success, -1 on error
 */
int cwrite(int prim_id, int scnd_id, char c);

/**
 * Read from a block device
 * 
 * @param prim_id Primary device ID (device selector)
 * @param scnd_id Secondary device ID (device-specific, e.g., partition number)
 * @param buf Buffer to read into
 * @param offset Starting block number (relative to device/partition)
 * @param count Number of blocks to read
 * @return Number of bytes read, or -1 on error
 */
int bread(int prim_id, int scnd_id, void *buf, uint32_t offset, size_t count);

/**
 * Write to a block device
 * 
 * @param prim_id Primary device ID (device selector)
 * @param scnd_id Secondary device ID (device-specific, e.g., partition number)
 * @param buf Buffer to write from
 * @param offset Starting block number (relative to device/partition)
 * @param count Number of blocks to write
 * @return Number of bytes written, or -1 on error
 */
int bwrite(int prim_id, int scnd_id, const void *buf, uint32_t offset, size_t count);

/**
 * Send ioctl command to a device (works for both char and block)
 * 
 * @param prim_id Primary device ID
 * @param scnd_id Secondary device ID
 * @param command Device-specific command code
 * @return Device-specific return value, or -1 on error
 */
int ioctl(int prim_id, int scnd_id, unsigned int command);

/**
 * Initialize the driver subsystem
 * Must be called before any device registration
 */
void driver_init(void);

#endif /* DRIVER_H */