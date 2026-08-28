#ifndef DRIVER_CHAR_MOUSE_H
#define DRIVER_CHAR_MOUSE_H

/* =========================================================================
 * PS/2 mouse driver
 *
 * Exposes /dev/mouse as a byte stream of 3-byte packets (Linux psaux /
 * mousedev style): [flags, dx, dy] where
 *   flags bit 0 = left button, bit 1 = right button, bit 2 = middle,
 *   bit 4 = dx sign, bit 5 = dy sign; dx/dy are 9-bit two's complement.
 * A single read(fd, buf, 3) returns one whole packet.
 * ========================================================================= */

void mouse_init(void);

#endif /* DRIVER_CHAR_MOUSE_H */
