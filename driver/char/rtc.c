/* ============================================================================
 * Real-Time Clock (RTC) Driver
 * 
 * Reads time from CMOS chip and provides /dev/rtc0 device
 * ============================================================================ */

#include "driver/char/rtc.h"
#include "driver/char/char.h"
#include "fs/devfs.h"
#include "lib/printk.h"
#include "kernel/asm.h"

/* =========================================================================
 * CMOS/RTC Hardware Interface
 * ========================================================================= */

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

/* CMOS register addresses */
#define RTC_SECONDS     0x00
#define RTC_MINUTES     0x02
#define RTC_HOURS       0x04
#define RTC_DAY         0x07
#define RTC_MONTH       0x08
#define RTC_YEAR        0x09
#define RTC_STATUS_A    0x0A
#define RTC_STATUS_B    0x0B

/* =========================================================================
 * Data Structures
 * ========================================================================= */

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} rtc_time_t;

/* =========================================================================
 * Helper Functions
 * ========================================================================= */

/**
 * Read a CMOS register (with NMI disabled during access)
 */
static uint8_t rtc_read_register(uint8_t reg)
{
    outb(CMOS_ADDR, (1 << 7) | reg);  /* Disable NMI + select register */
    return inb(CMOS_DATA);
}

/**
 * Convert BCD (Binary-Coded Decimal) to binary
 */
static uint8_t bcd_to_binary(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/**
 * Get current time from RTC
 */
static int rtc_get_time(rtc_time_t *time)
{
    /* Wait until update is not in progress */
    while (rtc_read_register(RTC_STATUS_A) & 0x80);
    
    /* Read all time/date registers */
    time->second = rtc_read_register(RTC_SECONDS);
    time->minute = rtc_read_register(RTC_MINUTES);
    time->hour   = rtc_read_register(RTC_HOURS);
    time->day    = rtc_read_register(RTC_DAY);
    time->month  = rtc_read_register(RTC_MONTH);
    time->year   = rtc_read_register(RTC_YEAR);
    
    /* Check if BCD mode (bit 2 of Status B = 0 means BCD) */
    uint8_t status_b = rtc_read_register(RTC_STATUS_B);
    if (!(status_b & 0x04)) {
        /* Convert from BCD to binary */
        time->second = bcd_to_binary(time->second);
        time->minute = bcd_to_binary(time->minute);
        time->hour   = bcd_to_binary(time->hour);
        time->day    = bcd_to_binary(time->day);
        time->month  = bcd_to_binary(time->month);
        time->year   = bcd_to_binary(time->year);
    }
    
    return 0;
}

/**
 * Get Unix timestamp from RTC
 * Returns seconds since 1970-01-01 00:00:00 UTC
 */
uint32_t rtc_get_unix_time(void)
{
    rtc_time_t time;
    if (rtc_get_time(&time) < 0) {
        return 1;  /* Fallback */
    }
    
    /* Calculate Unix timestamp
     * Note: This is a simplified calculation assuming years since 2000
     * A full implementation would need proper leap year handling */
    
    uint32_t year = 2000 + time.year;
    uint32_t days = 0;
    
    /* Days from years (rough approximation) */
    days += (year - 1970) * 365;
    days += (year - 1970 + 1) / 4;  /* Add leap years */
    
    /* Days from months (cumulative days at start of each month) */
    static const uint16_t days_in_month[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    if (time.month > 0 && time.month <= 12) {
        days += days_in_month[time.month - 1];
    }
    
    /* Add current day */
    days += time.day - 1;
    
    /* Convert to seconds */
    uint32_t timestamp = days * 86400;  /* Days to seconds */
    timestamp += time.hour * 3600;
    timestamp += time.minute * 60;
    timestamp += time.second;
    
    return timestamp;
}

/* =========================================================================
 * Driver Callbacks (stub implementations)
 * ========================================================================= */

static char rtc_read(int scnd_id)
{
    (void)scnd_id;
    rtc_time_t time;
    if (rtc_get_time(&time) == 0) {
        return (char)time.second;
    }
    return 0;
}

static int rtc_write(int scnd_id, char c)
{
    (void)scnd_id;
    (void)c;
    return 0;  /* Stub - no-op */
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void rtc_init(void)
{
    /* Register as char device 2 */
    char_ops_t ops = { 
        .read = rtc_read, 
        .write = rtc_write, 
        .ioctl = NULL 
    };
    register_char_device(2, &ops);
    
    /* Add devfs node */
    devfs_register_device("rtc0", DT_CHRDEV, 2, 0);
    
    printk("[RTC] Real-Time Clock driver initialized\n");
    
    /* Print system time */
    rtc_time_t time;
    if (rtc_get_time(&time) == 0) {
        printk("[RTC] System Time: 20%02u-%02u-%02u %02u:%02u:%02u\n",
               time.year, time.month, time.day,
               time.hour, time.minute, time.second);
    }
}