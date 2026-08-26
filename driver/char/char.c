#include "driver/char/char.h"
#include "driver/char/vga.h"
#include "driver/char/tty.h"
#include "driver/char/pit.h"
#include "driver/char/kbd.h"
#include "kernel/sched.h"
#include "errno.h"
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
    int               prim_id;
    char_ops_t        ops;
    wait_queue_head_t read_wq;   /* waiters blocked in char_read_block() */
    int               block_read; /* 1 = char_read_block() may sleep       */
    int               in_use;
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
            init_waitqueue_head(&char_devices[i].read_wq);
            char_devices[i].block_read = 0;
            char_devices[i].in_use = 1;
            return 0;
        }
    }
    return -1;   /* table full */
}

/* =========================================================================
 * Public API – I/O
 * ========================================================================= */

int cread(int prim_id, int scnd_id)
{
    char_device_t *dev = find_char_device(prim_id);
    if (!dev || !dev->ops.read)
        return 0;
    return dev->ops.read(scnd_id);
}

int char_read_block(int prim_id, int scnd_id)
{
    char_device_t *dev = find_char_device(prim_id);
    if (!dev || !dev->ops.read)
        return 0;

    /* Loop until a character is available. For blocking devices this
     * sleeps on the device's wait queue; the driver wakes us (e.g. the
     * keyboard IRQ) when data arrives. Must run with IF=0 (syscall
     * context) so the empty-check → sleep sequence has no missed wakeup.
     * A pending signal (e.g. Ctrl-C) aborts the read with -EINTR. */
    while (1) {
        int c = dev->ops.read(scnd_id);
        if (c)
            return c;
        if (!dev->block_read)
            return 0;   /* non-blocking device: report empty */
        if (current->pending)
            return -EINTR;
        sleep_on(&dev->read_wq);
    }
}

void char_wakeup(int prim_id)
{
    char_device_t *dev = find_char_device(prim_id);
    if (!dev)
        return;
    wake_up(&dev->read_wq);
    /* Also wake select()/poll() waiters so they re-check readiness. */
    extern void vfs_poll_wakeup(void);
    vfs_poll_wakeup();
}

int char_poll(int prim_id, int scnd_id)
{
    char_device_t *dev = find_char_device(prim_id);
    if (!dev || !dev->ops.poll)
        return 1;   /* no poll callback: assume always ready */
    return dev->ops.poll(scnd_id);
}

void char_set_blocking(int prim_id, int enable)
{
    char_device_t *dev = find_char_device(prim_id);
    if (!dev)
        return;
    dev->block_read = enable ? 1 : 0;
}

int cwrite(int prim_id, int scnd_id, char c)
{
    char_device_t *dev = find_char_device(prim_id);
    if (!dev || !dev->ops.write)
        return -1;
    return dev->ops.write(scnd_id, c);
}

int char_ioctl(int prim_id, int scnd_id, unsigned int command, uint32_t arg)
{
    char_device_t *dev = find_char_device(prim_id);
    if (!dev || !dev->ops.ioctl)
        return -1;
    return dev->ops.ioctl(prim_id, scnd_id, command, arg);
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
    miscdev_init(); /* /dev/null, /dev/zero: char devs 5, 6          */
}