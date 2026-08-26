/* =========================================================================
 * miscdev - /dev/null and /dev/zero character devices
 *
 *   /dev/null : reads return EOF (0 bytes), writes are discarded.
 *   /dev/zero : reads return zero-filled bytes, writes are discarded.
 *
 * Registered as char devices CHAR_DEV_NULL (5) and CHAR_DEV_ZERO (6).
 * ========================================================================= */

#include "driver/char/char.h"
#include "fs/devfs.h"

static int null_read(int scnd_id)
{
    (void)scnd_id;
    return 0;   /* EOF */
}

static int null_write(int scnd_id, char c)
{
    (void)scnd_id; (void)c;
    return 0;   /* discard */
}

static int null_poll(int scnd_id)
{
    (void)scnd_id;
    return 1;   /* always ready (EOF) */
}

static int zero_read(int scnd_id)
{
    (void)scnd_id;
    /* Note: the generic char read path can't express a NUL byte, so
     * devfs_read() special-cases CHAR_DEV_ZERO and memsets the buffer. */
    return 0;
}

static int zero_write(int scnd_id, char c)
{
    (void)scnd_id; (void)c;
    return 0;   /* discard */
}

static int zero_poll(int scnd_id)
{
    (void)scnd_id;
    return 1;
}

void miscdev_init(void)
{
    char_ops_t null_ops = {
        .read  = null_read,
        .write = null_write,
        .poll  = null_poll,
    };
    register_char_device(CHAR_DEV_NULL, &null_ops);
    devfs_register_device("null", DT_CHRDEV, CHAR_DEV_NULL, 0);

    char_ops_t zero_ops = {
        .read  = zero_read,
        .write = zero_write,
        .poll  = zero_poll,
    };
    register_char_device(CHAR_DEV_ZERO, &zero_ops);
    devfs_register_device("zero", DT_CHRDEV, CHAR_DEV_ZERO, 0);
}
