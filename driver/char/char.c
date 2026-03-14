#include "driver/char/char.h"
#include "driver/char/vga.h"
#include "driver/char/tty.h"
#include "driver/char/pit.h"
#include "driver/char/kbd.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Character device registry
 *
 * Uses a static fixed-size array so registration is allocation-free and
 * safe to call before buddy/slab are initialised.
 * ========================================================================= */

#define MAX_CHAR_DEVICES 16

typedef struct {
    int        prim_id;
    char_ops_t ops;
    int        in_use;
} char_device_t;

static char_device_t char_devices[MAX_CHAR_DEVICES];

/* -------------------------------------------------------------------------
 * Internal lookup
 * ------------------------------------------------------------------------- */

static char_device_t *find_char_device(int prim_id)
{
    for (int i = 0; i < MAX_CHAR_DEVICES; i++) {
        if (char_devices[i].in_use && char_devices[i].prim_id == prim_id)
            return &char_devices[i];
    }
    return NULL;
}

/* =========================================================================
 * Public API – registration
 * ========================================================================= */

int register_char_device(int prim_id, char_ops_t *ops)
{
    if (!ops || prim_id < 0 || prim_id > 255)
        return -1;

    if (find_char_device(prim_id))
        return -1;   /* already registered */

    for (int i = 0; i < MAX_CHAR_DEVICES; i++) {
        if (!char_devices[i].in_use) {
            char_devices[i].prim_id = prim_id;
            char_devices[i].ops    = *ops;
            char_devices[i].in_use = 1;
            return 0;
        }
    }
    return -1;   /* table full */
}

/* =========================================================================
 * Public API – I/O
 * ========================================================================= */

char cread(int prim_id, int scnd_id)
{
    char_device_t *dev = find_char_device(prim_id);
    if (!dev || !dev->ops.read)
        return 0;
    return dev->ops.read(scnd_id);
}

int cwrite(int prim_id, int scnd_id, char c)
{
    char_device_t *dev = find_char_device(prim_id);
    if (!dev || !dev->ops.write)
        return -1;
    return dev->ops.write(scnd_id, c);
}

int char_ioctl(int prim_id, int scnd_id, unsigned int command)
{
    char_device_t *dev = find_char_device(prim_id);
    if (!dev || !dev->ops.ioctl)
        return -1;
    return dev->ops.ioctl(prim_id, scnd_id, command);
}

/* =========================================================================
 * Aggregator – initialise all character devices in dependency order
 *
 * Each xxx_init() call handles its own driver registration and devfs node
 * creation internally.  This function is safe to call before the buddy and
 * slab allocators are ready because:
 *   - register_char_device() uses a static array (no kalloc)
 *   - devfs_register_device() uses a static array (no kalloc)
 *   - vga/tty/pit/kbd hardware init requires only I/O port access
 * ========================================================================= */

void char_init(void)
{
    vga_init();     /* VGA text mode: hw init + register char dev 0  */
    tty_init();     /* TTY emulator:  hw init + register char dev 2  */
    pit_init(100);  /* PIT @ 100 Hz:  hw init + register char dev 1  */
    kbd_init();     /* PS/2 kbd:      hw init + register char dev 3  */
}