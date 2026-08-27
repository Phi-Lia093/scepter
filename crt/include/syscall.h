/* ============================================================================
 * System Call Interface - ABI numbers + user-facing surface
 *
 * The syscall NUMBERS must stay in sync with kernel/include/kernel/syscall.h.
 * The user-facing declarations live in unistd.h / fcntl.h / etc.; including
 * this header pulls them in for backward compatibility.
 * ============================================================================ */

#ifndef _CRT_SYSCALL_H
#define _CRT_SYSCALL_H

/* Syscall numbers (Linux i386-compatible; see kernel/include/kernel/syscall.h) */
#define SYS_EXIT    1
#define SYS_FORK    2
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_WAITPID 7
#define SYS_LINK    9
#define SYS_UNLINK  10
#define SYS_EXECVE  11
#define SYS_CHDIR   12
#define SYS_TIME    13
#define SYS_MKNOD   14
#define SYS_CHMOD   15
#define SYS_LCHOWN  16
#define SYS_LSEEK   19
#define SYS_GETPID  20
#define SYS_MOUNT   21
#define SYS_UMOUNT  22
#define SYS_SETUID  23
#define SYS_GETUID  24
#define SYS_ALARM   27
#define SYS_UTIME   30
#define SYS_ACCESS  33
#define SYS_NICE    34
#define SYS_NANOSLEEP 35
#define SYS_SYNC    36
#define SYS_KILL    37
#define SYS_RENAME  38
#define SYS_MKDIR   39
#define SYS_RMDIR   40
#define SYS_DUP     41
#define SYS_PIPE    42
#define SYS_TIMES   43
#define SYS_BRK     45
#define SYS_SETGID  46
#define SYS_GETGID  47
#define SYS_SIGNAL  48
#define SYS_GETEUID 49
#define SYS_GETEGID 50
#define SYS_UMOUNT2 52
#define SYS_IOCTL   54
#define SYS_FCNTL   55
#define SYS_SETPGID 57
#define SYS_CHROOT  61
#define SYS_UMASK   60
#define SYS_DUP2    63
#define SYS_GETPPID 64
#define SYS_GETPGRP 65
#define SYS_SETSID  66
#define SYS_SIGACTION 67
#define SYS_SETREUID 70
#define SYS_SETREGID 71
#define SYS_SIGSUSPEND 72
#define SYS_SIGPENDING 73
#define SYS_SETHOSTNAME 74
#define SYS_SETRLIMIT 75
#define SYS_GETRLIMIT 76
#define SYS_GETRUSAGE 77
#define SYS_GETTIMEOFDAY 78
#define SYS_SETTIMEOFDAY 79
#define SYS_SYSINFO  116
#define SYS_GETGROUPS 80
#define SYS_SETGROUPS 81
#define SYS_SELECT  82
#define SYS_SYMLINK 83
#define SYS_READLINK 85
#define SYS_REBOOT  88
#define SYS_MMAP    90
#define SYS_MUNMAP  91
#define SYS_TRUNCATE 92
#define SYS_FTRUNCATE 93
#define SYS_FCHMOD  94
#define SYS_FCHOWN  95
#define SYS_GETPRIORITY 96
#define SYS_SETPRIORITY 97
#define SYS_STATFS  99
#define SYS_FSTATFS 100
#define SYS_SETITIMER 104
#define SYS_GETITIMER 105
#define SYS_STAT    106
#define SYS_LSTAT   107
#define SYS_FSTAT   108
#define SYS_WAIT4   114
#define SYS_FSYNC   118
#define SYS_SIGRETURN 119
#define SYS_SETDOMAINNAME 121
#define SYS_UNAME   122
#define SYS_MPROTECT 125
#define SYS_SIGPROCMASK 126
#define SYS_GETPGID 132
#define SYS_FCHDIR  133
#define SYS_FLOCK   143
#define SYS_PERSONALITY 136
#define SYS_SETFSUID 138
#define SYS_SETFSGID 139
#define SYS_GETDENTS 141
#define SYS_READV   145
#define SYS_WRITEV  146
#define SYS_GETSID  147
#define SYS_FDATASYNC 148
#define SYS_MLOCK   150
#define SYS_MUNLOCK 151
#define SYS_MLOCKALL 152
#define SYS_MUNLOCKALL 153
#define SYS_SCHED_SETPARAM 154
#define SYS_SCHED_GETPARAM 155
#define SYS_SCHED_GETSCHEDULER 157
#define SYS_SCHED_YIELD 158
#define SYS_SCHED_GET_PRIORITY_MAX 159
#define SYS_SCHED_GET_PRIORITY_MIN 160
#define SYS_SCHED_RR_GET_INTERVAL 161
#define SYS_POLL    168
#define SYS_PRCTL   172
#define SYS_PREAD   180
#define SYS_PWRITE  181
#define SYS_CHOWN   182
#define SYS_GETCWD  183
#define SYS_SENDFILE 187
#define SYS_FADVISE64 221
#define SYS_EXIT_GROUP 252
#define SYS_SET_TID_ADDRESS 258
#define SYS_CLOCK_GETTIME 265
#define SYS_CLOCK_GETRES 266
#define SYS_CLOCK_NANOSLEEP 267
#define SYS_TGKILL  270
#define SYS_UTIMES  271
#define SYS_SYNCFS  306
#define SYS_GETCPU  318
#define SYS_DUP3    330
#define SYS_PIPE2   331
#define SYS_MEMBARRIER 375
#define SYS_CLOSE_RANGE 436
#define SYS_SETRESUID 164
#define SYS_GETRESUID 165
#define SYS_SETRESGID 170
#define SYS_GETRESGID 171

/* Standard file descriptors (compat) */
#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#include "unistd.h"
#include "fcntl.h"

#endif /* _CRT_SYSCALL_H */
