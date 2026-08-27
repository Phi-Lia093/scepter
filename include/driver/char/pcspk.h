#ifndef DRIVER_CHAR_PCSPK_H
#define DRIVER_CHAR_PCSPK_H

#include <stdint.h>

/* =========================================================================
 * PC Speaker driver (/dev/pcspk)
 *
 * Drives the PIT channel 2 / speaker-interface (ports 0x42/0x43/0x61).
 * The speaker cannot produce sound until the 8253's gate input is enabled;
 * this driver toggles the gate bits on port 0x61 directly.
 * ========================================================================= */

/** ioctl commands for /dev/pcspk (must match crt/include/sys/ioctl.h) */
#define IOCTL_PCSPK_BEEP 1   /* arg = frequency in Hz; 0 = silence */
#define IOCTL_PCSPK_GET  2   /* return current frequency */

/** Start a tone at the given frequency (Hz).  freq == 0 silences. */
void pcspk_beep(uint32_t freq);

/** Silence the speaker. */
void pcspk_off(void);

/** Register /dev/pcspk as a char device. */
void pcspk_init(void);

#endif /* DRIVER_CHAR_PCSPK_H */
