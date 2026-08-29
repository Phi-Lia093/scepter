#ifndef ARCH_TIMER_H
#define ARCH_TIMER_H

#include <stdint.h>
#include "kernel/sched.h"

/* =========================================================================
 * System timer – arch-neutral API.
 *
 * The arch timer provides the periodic scheduling clock (IRQ0 on i386),
 * a global tick counter, and the wait queue woken on every tick
 * (used by nanosleep(), select()/poll() timeouts, ...).
 * ========================================================================= */

/**
 * arch_timer_init - Initialize the system timer at the given frequency.
 * @param hz  Frequency in Hz (100 = 10 ms ticks)
 */
void arch_timer_init(uint32_t hz);

/**
 * arch_timer_get_ticks - Returns the total tick count since boot.
 */
uint32_t arch_timer_get_ticks(void);

/**
 * arch_timer_read_count - Read the free-running timer counter.
 * @return Current counter value (implementation-defined units).  Used by
 *         drivers for short busy-waits while interrupts are disabled.
 */
uint16_t arch_timer_read_count(void);

/* Wait queue woken on every timer tick; used by nanosleep() etc. */
extern wait_queue_head_t timer_wq;

#endif /* ARCH_TIMER_H */
