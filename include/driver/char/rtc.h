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

#endif /* RTC_H */