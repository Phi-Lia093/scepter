#ifndef DRIVER_CHAR_H
#define DRIVER_CHAR_H

#include <stdint.h>

/* =========================================================================
 * Shared ioctl callback type (guarded so block.h can define the same)
 * ========================================================================= */
#ifndef IOCTL_FN_T
#define IOCTL_FN_T
typedef int (*ioctl_fn)(int prim_id, int scnd_id, unsigned int command, uint32_t arg);
#endif

/* =========================================================================
 * Character device callback types
 * ========================================================================= */
typedef int  (*char_read_fn)(int scnd_id);
typedef int  (*char_write_fn)(int scnd_id, char c);

typedef struct {
    char_read_fn  read;
    char_write_fn write;
    ioctl_fn      ioctl;
} char_ops_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/** Register a character device (prim_id 0-255). Returns 0 or -1. */
int  register_char_device(int prim_id, char_ops_t *ops);

/** Read one character from char device prim_id. Returns 0 on error. */
int cread(int prim_id, int scnd_id);

/**
 * Read one character from char device prim_id, blocking until data arrives.
 * Only blocks for devices that enabled blocking via char_set_blocking();
 * for all other devices this behaves exactly like cread().
 * Returns 0 if the device is absent / has no read callback,
 * and -EINTR if a signal is pending (the caller should retry or abort).
 */
int char_read_block(int prim_id, int scnd_id);

/**
 * Wake all processes blocked in char_read_block() on the given device.
 * Called by drivers (e.g. the keyboard IRQ) when new data is available.
 */
void char_wakeup(int prim_id);

/**
 * Enable/disable blocking reads on a device.
 * @param enable 1 = char_read_block() sleeps while the device is empty
 */
void char_set_blocking(int prim_id, int enable);

/** Write one character to char device prim_id. Returns 0 or -1. */
int  cwrite(int prim_id, int scnd_id, char c);

/** Send an ioctl command to a char device. Returns device value or -1. */
int  char_ioctl(int prim_id, int scnd_id, unsigned int command, uint32_t arg);

/** Short alias for char_ioctl */
#define cioctl char_ioctl

/**
 * Initialise all character devices: VGA, TTY, PIT, keyboard.
 * Safe to call before buddy/slab (uses no dynamic allocation).
 */
void char_init(void);

#endif /* DRIVER_CHAR_H */