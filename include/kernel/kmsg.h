#ifndef KERNEL_KMSG_H
#define KERNEL_KMSG_H

#include <stdint.h>

/* =========================================================================
 * Kernel message ring buffer (/dev/kmsg)
 *
 * Every character emitted by printk() (both early and normal output) is
 * appended to a fixed-size circular buffer.  Userland reads complete lines
 * through the /dev/kmsg character device.
 * ========================================================================= */

/** Append one character to the kernel log ring buffer.  IRQ-safe. */
void kmsg_putchar(char c);

/**
 * Copy the next complete line (including trailing '\n') into buf.
 * Returns the number of bytes copied, or 0 when no complete line is
 * available yet.
 */
int kmsg_read(char *buf, int max);

/** Return 1 when at least one complete line is buffered. */
int kmsg_poll(void);

/** Register the /dev/kmsg character device. */
void kmsg_init(void);

#endif /* KERNEL_KMSG_H */
