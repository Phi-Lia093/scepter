#ifndef PIT_H
#define PIT_H

#include <stdint.h>

/* =========================================================================
 * 8253/8254 Programmable Interval Timer – channel 0 (IRQ0)
 * ========================================================================= */

/* I/O ports */
#define PIT_CHANNEL0  0x40   /* channel 0 data port  */
#define PIT_CMD       0x43   /* mode/command register */

/* Command byte: channel 0, lobyte/hibyte access, mode 3 (square wave) */
#define PIT_CMD_INIT  0x36

/* PIT input clock frequency (Hz) */
#define PIT_BASE_HZ   1193182UL

/*
 * Initialise PIT channel 0 at the given frequency (Hz).
 * Registers the IRQ0 handler in the IDT and unmasks IRQ0 in the PIC.
 */
void pit_init(uint32_t hz);

/* Returns the total tick count since pit_init() */
uint32_t pit_get_ticks(void);

#endif /* PIT_H */
