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

#endif /* RTC_H */