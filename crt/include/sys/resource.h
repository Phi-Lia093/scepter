#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#include <sys/types.h>
#include <sys/time.h>

typedef long rlim_t;

struct rlimit {
    rlim_t rlim_cur;   /* soft limit */
    rlim_t rlim_max;   /* hard limit */
};

#define RLIM_INFINITY ((rlim_t)-1)

#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#define RLIMIT_RSS     5
#define RLIMIT_NPROC   6
#define RLIMIT_NOFILE  7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS      9
#define RLIMIT_LOCKS  10

int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);

/* ---- getrusage ---- */

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN 1

struct rusage {
    struct timeval ru_utime;   /* user CPU time used      */
    struct timeval ru_stime;   /* system CPU time used    */
    long ru_maxrss;            /* maximum resident set size */
    long ru_ixrss;             /* integral shared memory size */
    long ru_idrss;             /* integral unshared data size */
    long ru_isrss;             /* integral unshared stack size */
    long ru_minflt;            /* page reclaims (soft page faults) */
    long ru_majflt;            /* page faults (hard page faults) */
    long ru_nswap;             /* swaps */
    long ru_inblock;           /* block input operations */
    long ru_oublock;           /* block output operations */
    long ru_msgsnd;            /* IPC messages sent */
    long ru_msgrcv;            /* IPC messages received */
    long ru_nsignals;          /* signals received */
    long ru_nvcsw;             /* voluntary context switches */
    long ru_nivcsw;            /* involuntary context switches */
};

int getrusage(int who, struct rusage *usage);

/* ---- getpriority / setpriority ---- */

#define PRIO_PROCESS 0
#define PRIO_PGRP    1
#define PRIO_USER    2

int getpriority(int which, int who);
int setpriority(int which, int who, int niceval);

#endif /* _SYS_RESOURCE_H */
