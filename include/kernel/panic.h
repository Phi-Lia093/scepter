#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

/* =========================================================================
 * Kernel panic
 *
 * The exception entry point (panic_isr, arch-specific register dump) is
 * implemented by the active arch in arch/<arch>/trap.c.
 * ========================================================================= */

/* General kernel panic – prints message, disables interrupts, halts */
void panic(const char *msg);

#endif /* PANIC_H */
