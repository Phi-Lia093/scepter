/* ============================================================================
 * System Call Interface - ABI numbers + user-facing surface
 *
 * The syscall NUMBERS must stay in sync with kernel/include/kernel/syscall.h.
 * The user-facing declarations live in unistd.h / fcntl.h / etc.; including
 * this header pulls them in for backward compatibility.
 * ============================================================================ */

#ifndef _CRT_SYSCALL_H
#define _CRT_SYSCALL_H

/* Syscall numbers (Linux-compatible subset) */
#define SYS_EXIT   1
#define SYS_FORK   2
#define SYS_READ   3
#define SYS_WRITE  4
#define SYS_OPEN   5
#define SYS_CLOSE  6
#define SYS_WAIT4  7
#define SYS_LINK   9
#define SYS_EXEC   10   /* exec(path) - no args               */
#define SYS_EXECVE 11   /* execve(path, argv, envp)           */
#define SYS_CHDIR  12   /* Change working directory           */
#define SYS_EXECV  13   /* execv(path, argv) - no envp        */
#define SYS_MKNOD  14
#define SYS_CHMOD  15
#define SYS_LSEEK  19
#define SYS_GETPID 20
#define SYS_SETUID 23
#define SYS_GETUID 24
#define SYS_ACCESS 33
#define SYS_NICE   34
#define SYS_NANOSLEEP 35
#define SYS_KILL   37
#define SYS_RENAME 38
#define SYS_MKDIR  39
#define SYS_RMDIR  40
#define SYS_DUP    41
#define SYS_PIPE   42
#define SYS_BRK    45
#define SYS_SETGID 46
#define SYS_GETGID 47
#define SYS_SIGNAL 48
#define SYS_GETEUID 49
#define SYS_GETEGID 50
#define SYS_IOCTL  54
#define SYS_FCNTL  55
#define SYS_SETPGID 57
#define SYS_UMASK  60
#define SYS_DUP2   63
#define SYS_GETPPID 64
#define SYS_GETPGRP 65
#define SYS_SETSID 66
#define SYS_SIGACTION 67
#define SYS_SIGSUSPEND 72
#define SYS_SIGPENDING 73
#define SYS_GETTIMEOFDAY 78
#define SYS_SELECT 82
#define SYS_SYMLINK 83
#define SYS_READLINK 85
#define SYS_MMAP   90
#define SYS_MUNMAP 91
#define SYS_TRUNCATE 92
#define SYS_FTRUNCATE 93
#define SYS_FCHMOD 94
#define SYS_STAT   106
#define SYS_LSTAT  107
#define SYS_FSTAT  108
#define SYS_FSYNC  118
#define SYS_SIGRETURN 119
#define SYS_UNAME  122
#define SYS_SIGPROCMASK 126
#define SYS_GETPGID 132
#define SYS_UNLINK 137
#define SYS_GETDENTS 141
#define SYS_READV 145
#define SYS_WRITEV 146
#define SYS_GETSID 147
#define SYS_FDATASYNC 148
#define SYS_POLL  168
#define SYS_PREAD 180
#define SYS_PWRITE 181
#define SYS_GETCWD 183
#define SYS_DUP3 330

/* Standard file descriptors (compat) */
#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#include "unistd.h"
#include "fcntl.h"

#endif /* _CRT_SYSCALL_H */
