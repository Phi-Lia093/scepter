#ifndef KBD_H
#define KBD_H

#include <stdint.h>

/**
 * PS/2 Keyboard Driver
 * 
 * Provides keyboard input via IRQ1 interrupt handler.
 * Characters are buffered in a circular buffer and retrieved via read().
 * Supports basic ASCII input with modifier keys (Shift, Caps Lock).
 */

/**
 * Initialize keyboard driver.
 * Sets up IRQ1 handler, buffer, driver registration and devfs node.
 */
void kbd_init(void);

#endif /* KBD_H */