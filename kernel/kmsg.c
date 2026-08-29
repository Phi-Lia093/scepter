#include "kernel/kmsg.h"
#include "driver/char/char.h"
#include "fs/devfs.h"
#include "arch/cpu.h"
#include "lib/printk.h"
#include <stdint.h>

/* =========================================================================
 * Kernel message ring buffer (/dev/kmsg)
 *
 * printk() appends every emitted character here (see lib/printk.c).  The
 * buffer is a fixed circular byte ring; reads hand out whole lines so the
 * dmesg utility gets line-aligned output even when the ring has wrapped.
 *
 * IRQ safety: printk can run in interrupt context, so every ring access
 * happens with interrupts disabled (cli/sti).  All state is static — no
 * allocation, safe from the very first boot printk.
 * ========================================================================= */

#define KMSG_BUFSIZE 8192

static char kmsg_buf[KMSG_BUFSIZE];

/* Next position to write. */
static volatile uint32_t kmsg_write_pos;
/* Position where the next (partial) line starts. */
static volatile uint32_t kmsg_read_pos;

void kmsg_putchar(char c)
{
    unsigned long flags = irq_save();

    kmsg_buf[kmsg_write_pos] = c;
    uint32_t next = (kmsg_write_pos + 1) % KMSG_BUFSIZE;
    if (next == kmsg_read_pos) {
        /* Ring full: drop the oldest byte.  read_pos stays line-start-ish;
         * a dropped partial line simply resumes at whatever byte follows. */
        kmsg_read_pos = (kmsg_read_pos + 1) % KMSG_BUFSIZE;
    }
    kmsg_write_pos = next;

    irq_restore(flags);
}

int kmsg_poll(void)
{
    uint32_t pos = kmsg_read_pos;
    while (pos != kmsg_write_pos) {
        if (kmsg_buf[pos] == '\n')
            return 1;
        pos = (pos + 1) % KMSG_BUFSIZE;
    }
    return 0;
}

int kmsg_read(char *buf, int max)
{
    if (!buf || max <= 0)
        return 0;

    unsigned long flags = irq_save();

    /* Measure the distance to the next '\n' (or the write cursor). */
    uint32_t pos = kmsg_read_pos;
    int len = 0;
    int has_nl = 0;
    while (pos != kmsg_write_pos) {
        char c = kmsg_buf[pos];
        pos = (pos + 1) % KMSG_BUFSIZE;
        len++;
        if (c == '\n') {
            has_nl = 1;
            break;
        }
        if (len >= max)
            break;
    }

    if (!has_nl) {
        irq_restore(flags);
        return 0;   /* no complete line yet */
    }

    /* Copy the line out. */
    int copied = 0;
    pos = kmsg_read_pos;
    while (copied < len && copied < max) {
        buf[copied++] = kmsg_buf[pos];
        pos = (pos + 1) % KMSG_BUFSIZE;
    }
    kmsg_read_pos = pos;

    irq_restore(flags);
    return copied;
}

/* -------------------------------------------------------------------------
 * Character device (/dev/kmsg)
 * ------------------------------------------------------------------------- */

static int kmsg_dev_read(int scnd_id)
{
    (void)scnd_id;
    return 0;   /* devfs_read special-cases CHAR_DEV_KMSG for bulk lines */
}

static int kmsg_dev_write(int scnd_id, char c)
{
    (void)scnd_id;
    (void)c;
    return -1;  /* read-only */
}

static int kmsg_dev_poll(int scnd_id)
{
    (void)scnd_id;
    return kmsg_poll();
}

static int kmsg_dev_ioctl(int prim_id, int scnd_id, unsigned int command,
                          uint32_t arg)
{
    (void)prim_id;
    (void)scnd_id;
    (void)command;
    (void)arg;
    return -1;
}

void kmsg_init(void)
{
    kmsg_write_pos = 0;
    kmsg_read_pos  = 0;

    char_ops_t ops = {
        .read  = kmsg_dev_read,
        .write = kmsg_dev_write,
        .poll  = kmsg_dev_poll,
        .ioctl = kmsg_dev_ioctl,
    };
    register_char_device(CHAR_DEV_KMSG, &ops);
    devfs_register_device("kmsg", DT_CHRDEV, CHAR_DEV_KMSG, 0);

    printk("[KMSG] /dev/kmsg registered (kernel log ring buffer)\n");
}
