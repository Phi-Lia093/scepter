#ifndef _TIME_H
#define _TIME_H

/* i386: both fields are 32-bit.  Layout must match the kernel's
 * timespec_t / timeval_t in include/kernel/syscall.h. */

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

int nanosleep(const struct timespec *req, struct timespec *rem);
int gettimeofday(struct timeval *tv, void *tz);
unsigned int sleep(unsigned int seconds);

#endif /* _TIME_H */
