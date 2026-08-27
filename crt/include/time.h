#ifndef _TIME_H
#define _TIME_H

#include <sys/types.h>   /* time_t, clock_t */

/* i386: both fields are 32-bit.  Layout must match the kernel's
 * timespec_t / timeval_t in include/kernel/syscall.h. */

typedef int clockid_t;

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

/* Clock ids (must match kernel CLOCK_* values in syscall.c) */
#define CLOCK_REALTIME            0
#define CLOCK_MONOTONIC           1
#define CLOCK_PROCESS_CPUTIME_ID  2

#define CLOCKS_PER_SEC 100   /* PIT tick rate */

int nanosleep(const struct timespec *req, struct timespec *rem);
int gettimeofday(struct timeval *tv, void *tz);
unsigned int sleep(unsigned int seconds);
time_t time(time_t *tloc);
int clock_gettime(clockid_t clockid, struct timespec *ts);
int clock_getres(clockid_t clockid, struct timespec *ts);

/* Broken-down time */
struct tm {
    int tm_sec;    /* seconds after the minute [0-60]  */
    int tm_min;    /* minutes after the hour [0-59]    */
    int tm_hour;   /* hours since midnight [0-23]      */
    int tm_mday;   /* day of the month [1-31]          */
    int tm_mon;    /* months since January [0-11]      */
    int tm_year;   /* years since 1900                 */
    int tm_wday;   /* days since Sunday [0-6]          */
    int tm_yday;   /* days since January 1 [0-365]     */
    int tm_isdst;  /* daylight saving flag             */
};

struct tm *localtime(const time_t *timep);
struct tm *gmtime(const time_t *timep);
time_t mktime(struct tm *tm);
char *asctime(const struct tm *tm);
char *ctime(const time_t *timep);
double difftime(time_t time1, time_t time0);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);

#endif /* _TIME_H */


int clock_nanosleep(clockid_t clockid, int flags,
                    const struct timespec *rqtp, struct timespec *rmtp);
int utimes(const char *path, const struct timeval times[2]);
