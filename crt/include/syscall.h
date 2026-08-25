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
#define SYS_EXEC   10   /* exec(path) - no args               */
#define SYS_EXECVE 11   /* execve(path, argv, envp)           */
#define SYS_CHDIR  12   /* Change working directory           */
#define SYS_EXECV  13   /* execv(path, argv) - no envp        */
#define SYS_LSEEK  19
#define SYS_GETPID 20
#define SYS_NICE   34
#define SYS_NANOSLEEP 35
#define SYS_KILL   37
#define SYS_RENAME 38
#define SYS_MKDIR  39
#define SYS_RMDIR  40
#define SYS_DUP    41
#define SYS_PIPE   42
#define SYS_BRK    45
#define SYS_SIGNAL 48
#define SYS_IOCTL  54
#define SYS_DUP2   63
#define SYS_GETPPID 64
#define SYS_GETTIMEOFDAY 78
#define SYS_MMAP   90
#define SYS_MUNMAP 91
#define SYS_TRUNCATE 92
#define SYS_STAT   106
#define SYS_FSTAT  108
#define SYS_SIGRETURN 119
#define SYS_UNAME  122
#define SYS_UNLINK 137
#define SYS_GETDENTS 141
#define SYS_GETCWD 183
#define SYS_ACCESS 33

/* Standard file descriptors (compat) */
#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#include "unistd.h"
#include "fcntl.h"

#endif /* _CRT_SYSCALL_H */
