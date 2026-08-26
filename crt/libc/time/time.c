/* ============================================================================
 * Time conversion: localtime/gmtime/mktime/asctime/ctime/strftime/difftime.
 *
 * Scepter does not track a timezone, so localtime() == gmtime() (UTC).
 * ============================================================================ */

#include <time.h>
#include <string.h>
#include <stdio.h>

/* Days from civil epoch (1970-01-01) to the given civil date. */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

/* Civil date from days since epoch (Howard Hinnant's inverse). */
static void civil_from_days(long z, int *y, int *m, int *d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned mm = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yy + (mm <= 2));
    *m = (int)mm;
    *d = (int)dd;
}

static struct tm g_tm;

/* Convert a Unix timestamp to a struct tm (UTC). */
static struct tm *time_to_tm(time_t t)
{
    long secs = (long)t;
    int days = (int)(secs / 86400);
    int rem = (int)(secs % 86400);
    if (rem < 0) {
        rem += 86400;
        days--;
    }

    int y, m, d;
    civil_from_days(days, &y, &m, &d);

    g_tm.tm_sec  = rem % 60;
    g_tm.tm_min  = (rem / 60) % 60;
    g_tm.tm_hour = rem / 3600;
    g_tm.tm_mday = d;
    g_tm.tm_mon  = m - 1;
    g_tm.tm_year = y - 1900;
    g_tm.tm_wday = (days + 4) % 7;    /* 1970-01-01 was a Thursday */
    if (g_tm.tm_wday < 0) g_tm.tm_wday += 7;
    g_tm.tm_yday = (int)(days - days_from_civil(y, 1, 1));
    g_tm.tm_isdst = 0;
    return &g_tm;
}

struct tm *gmtime(const time_t *timep)
{
    return time_to_tm(*timep);
}

struct tm *localtime(const time_t *timep)
{
    /* No timezone support: local == UTC. */
    return time_to_tm(*timep);
}

/* Convert a struct tm to a Unix timestamp. */
time_t mktime(struct tm *tm)
{
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon + 1;
    int d = tm->tm_mday;

    long days = days_from_civil(y, m, d);
    long secs = days * 86400 +
                tm->tm_hour * 3600 +
                tm->tm_min * 60 +
                tm->tm_sec;

    /* Normalize through the inverse so out-of-range fields are corrected. */
    struct tm *norm = time_to_tm((time_t)secs);
    *tm = *norm;
    tm->tm_isdst = 0;
    return (time_t)secs;
}

char *asctime(const struct tm *tm)
{
    static char buf[26];
    static const char *wdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    snprintf(buf, sizeof(buf), "%.3s %.3s %2d %02d:%02d:%02d %d\n",
             wdays[tm->tm_wday % 7], months[tm->tm_mon % 12],
             tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
             tm->tm_year + 1900);
    return buf;
}

char *ctime(const time_t *timep)
{
    return asctime(localtime(timep));
}

double difftime(time_t time1, time_t time0)
{
    return (double)((long)time1 - (long)time0);
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
    if (!s || !format || !tm || max == 0)
        return 0;

    size_t n = 0;
    static const char *wdays[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };
    static const char *wday_abbr[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    static const char *month_abbr[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    for (const char *p = format; *p && n < max - 1; p++) {
        char tmp[32];
        int len = 0;

        if (*p != '%') {
            s[n++] = *p;
            continue;
        }
        p++;
        switch (*p) {
            case 'Y': len = snprintf(tmp, sizeof(tmp), "%d", tm->tm_year + 1900); break;
            case 'y': len = snprintf(tmp, sizeof(tmp), "%02d", (tm->tm_year + 1900) % 100); break;
            case 'm': len = snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mon + 1); break;
            case 'd': len = snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mday); break;
            case 'H': len = snprintf(tmp, sizeof(tmp), "%02d", tm->tm_hour); break;
            case 'M': len = snprintf(tmp, sizeof(tmp), "%02d", tm->tm_min); break;
            case 'S': len = snprintf(tmp, sizeof(tmp), "%02d", tm->tm_sec); break;
            case 'j': len = snprintf(tmp, sizeof(tmp), "%03d", tm->tm_yday + 1); break;
            case 'A': len = snprintf(tmp, sizeof(tmp), "%s", wdays[tm->tm_wday % 7]); break;
            case 'a': len = snprintf(tmp, sizeof(tmp), "%s", wday_abbr[tm->tm_wday % 7]); break;
            case 'B': len = snprintf(tmp, sizeof(tmp), "%s", months[tm->tm_mon % 12]); break;
            case 'b':
            case 'h': len = snprintf(tmp, sizeof(tmp), "%s", month_abbr[tm->tm_mon % 12]); break;
            case 'C': len = snprintf(tmp, sizeof(tmp), "%02d", (tm->tm_year + 1900) / 100); break;
            case 'D': len = snprintf(tmp, sizeof(tmp), "%02d/%02d/%02d",
                                     tm->tm_mon + 1, tm->tm_mday,
                                     (tm->tm_year + 1900) % 100); break;
            case 'T': len = snprintf(tmp, sizeof(tmp), "%02d:%02d:%02d",
                                     tm->tm_hour, tm->tm_min, tm->tm_sec); break;
            case 'e': len = snprintf(tmp, sizeof(tmp), "%2d", tm->tm_mday); break;
            case '%': len = snprintf(tmp, sizeof(tmp), "%%"); break;
            default:
                tmp[0] = '%';
                tmp[1] = *p ? *p : '%';
                tmp[2] = '\0';
                len = *p ? 2 : 1;
                if (!*p)
                    p--;
                break;
        }

        if (len < 0)
            len = 0;
        for (int i = 0; i < len && n < max - 1; i++)
            s[n++] = tmp[i];
    }
    s[n] = '\0';
    return n;
}
