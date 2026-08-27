#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/**
 * Initialize the RTC driver
 * Registers /dev/rtc0 and prints system time
 */
void rtc_init(void);

/**
 * Get current time as Unix timestamp
 * @return Seconds since 1970-01-01 00:00:00 UTC
 */
uint32_t rtc_get_unix_time(void);

/**
 * Get the Unix timestamp captured when the RTC driver was initialized.
 * Used by gettimeofday(): wall clock = boot time + PIT uptime.
 * @return Seconds since 1970-01-01 00:00:00 UTC at boot
 */
uint32_t rtc_get_boot_unix_time(void);

/**
 * Get the unadjusted boot timestamp (without the settimeofday() offset).
 * Used to compute absolute time offsets in settimeofday().
 */
uint32_t rtc_get_real_boot_unix_time(void);

/**
 * Set the wall-clock adjustment applied by settimeofday().
 * The returned boot time is (real boot time + this offset).
 * @param delta Offset in seconds added to the real boot time.
 */
void rtc_set_time_offset(int32_t delta);

#endif /* RTC_H */