#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H

/* clock_t: number of ticks; CLOCKS_PER_SEC is 100 (PIT). */
typedef long clock_t;

struct tms {
    clock_t tms_utime;   /* user CPU time   */
    clock_t tms_stime;   /* system CPU time */
    clock_t tms_cutime;  /* children user   */
    clock_t tms_cstime;  /* children system */
};

clock_t times(struct tms *buf);

#endif /* _SYS_TIMES_H */
