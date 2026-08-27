#ifndef DRIVER_CHAR_RANDOM_H
#define DRIVER_CHAR_RANDOM_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * /dev/random and /dev/urandom
 *
 * A small CSPRNG (xorshift128) seeded from RTC/PIT/keyboard jitter.
 * This kernel has no real hardware entropy source, so the pool is seeded
 * once at boot and continuously mixed with interrupt timing jitter; both
 * devices draw from the same generator.
 * ========================================================================= */

/** Fill a buffer with random bytes.  Never fails. */
void random_get_bytes(void *buf, size_t n);

/** Get one 32-bit random value. */
uint32_t random_get_u32(void);

/**
 * Mix some entropy (e.g. interrupt timing jitter) into the pool.
 * Safe to call from interrupt context.
 */
void random_add_entropy(uint32_t bits);

/** Report the number of bits of entropy the pool currently holds. */
uint32_t random_entropy_count(void);

/** Register /dev/random and /dev/urandom as char devices. */
void random_init(void);

#endif /* DRIVER_CHAR_RANDOM_H */
