#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <time.h>   /* struct timeval (must match kernel timeval_t) */

/* Interval timer for setitimer/getitimer. */
struct itimerval {
    struct timeval it_interval;  /* timer period */
    struct timeval it_value;     /* time to next expiration */
};

/* Which timer (only ITIMER_REAL is implemented). */
#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

int getitimer(int which, struct itimerval *value);
int setitimer(int which, const struct itimerval *new_value,
              struct itimerval *old_value);

int gettimeofday(struct timeval *tv, void *tz);
int settimeofday(const struct timeval *tv, const void *tz);

#endif /* _SYS_TIME_H */
