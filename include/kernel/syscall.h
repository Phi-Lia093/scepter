#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * CPU Register State (for fork context preservation)
 * ========================================================================= */

typedef struct registers {
    /* Pushed by pusha */
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    
    /* Segment selectors */
    uint32_t gs, fs, es, ds;
    
    /* Saved CR3 */
    uint32_t cr3;
    
    /* IRET frame (pushed by CPU) */
    uint32_t eip, cs, eflags, user_esp, ss;
} registers_t;

/* =========================================================================
 * System Call Numbers (Linux i386-compatible)
 *
 * All numbers match Linux's i386 syscall table (arch/x86/entry/syscalls/
 * syscall_32.tbl).  Our own extensions are kept off the standard range.
 * ========================================================================= */

/* ---- Process (1..2) ---- */
#define SYS_EXIT    1
#define SYS_FORK    2

/* ---- File I/O (3..8) ---- */
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_WAITPID 7
#define SYS_CREAT   8

#define SYS_LINK    9    /* Hard link */
#define SYS_UNLINK  10   /* Delete a file */
#define SYS_EXECVE  11   /* execve(path, argv, envp) */
#define SYS_CHDIR   12   /* Change working directory */
#define SYS_TIME    13   /* Get seconds since epoch */
#define SYS_MKNOD   14   /* Create a device node */
#define SYS_CHMOD   15   /* Change permission bits */
#define SYS_LCHOWN  16   /* Change owner of a symlink (no follow) */

/* ---- Misc syscalls (19..34) ---- */
#define SYS_LSEEK   19   /* Reposition file offset */
#define SYS_GETPID  20   /* Get process ID */
#define SYS_MOUNT   21   /* Mount a filesystem */
#define SYS_UMOUNT  22   /* Unmount a filesystem */
#define SYS_SETUID  23   /* Set real + effective user id */
#define SYS_GETUID  24   /* Get real user id */
#define SYS_ALARM   27   /* Schedule SIGALRM after N seconds */
#define SYS_ACCESS  33   /* Check file accessibility */
#define SYS_NICE    34   /* Adjust the calling process's priority */

/* ---- Signals / time / memory (35..60) ---- */
#define SYS_NANOSLEEP 35  /* Sleep for specified time (PIT tick based) */
#define SYS_SYNC    36   /* Flush all pending disk writes */
#define SYS_KILL    37   /* Send a signal to a process */
#define SYS_RENAME  38   /* Rename / move a file or directory */
#define SYS_MKDIR   39   /* Create a directory */
#define SYS_RMDIR   40   /* Remove an empty directory */
#define SYS_DUP     41   /* Duplicate file descriptor */
#define SYS_PIPE    42   /* Create an anonymous pipe */
#define SYS_TIMES   43   /* Get process and system times */
#define SYS_BRK     45
#define SYS_SETGID  46   /* Set real + effective group id */
#define SYS_GETGID  47   /* Get real group id */
#define SYS_SIGNAL  48   /* Install a signal handler */
#define SYS_GETEUID 49   /* Get effective user id */
#define SYS_GETEGID 50   /* Get effective group id */
#define SYS_UMOUNT2 52   /* Unmount with flags */
#define SYS_IOCTL   54   /* I/O control */
#define SYS_FCNTL   55   /* File control (fcntl) */
#define SYS_SETPGID 57   /* Set process group */
#define SYS_CHROOT  61   /* Change root directory (root only) */
#define SYS_UMASK   60   /* Set file creation mask */

/* ---- Duplicate fds / process groups (63..73) ---- */
#define SYS_DUP2    63   /* Duplicate file descriptor to specific fd */
#define SYS_GETPPID 64   /* Get parent process ID */
#define SYS_GETPGRP 65   /* Get process group of calling process */
#define SYS_SETSID  66   /* Create a new session */
#define SYS_SIGACTION 67 /* Install a signal handler (POSIX) */
#define SYS_SETREUID 70  /* Set real + effective user id (separately) */
#define SYS_SETREGID 71  /* Set real + effective group id (separately) */
#define SYS_SIGSUSPEND 72 /* Wait for a signal */
#define SYS_SIGPENDING 73 /* Examine pending signals */

/* ---- Hostname / rlimits / time (74..79) ---- */
#define SYS_SETHOSTNAME 74   /* Set the hostname */
#define SYS_SETRLIMIT 75     /* Set resource limits */
#define SYS_GETRLIMIT 76     /* Get resource limits */
#define SYS_GETRUSAGE 77     /* Get resource usage */
#define SYS_GETTIMEOFDAY 78  /* Get wall-clock time */
#define SYS_SETTIMEOFDAY 79  /* Set wall-clock time (root only) */
#define SYS_SYSINFO   116    /* Get system information */

/* ---- Groups / select / links (80..88) ---- */
#define SYS_GETGROUPS 80     /* Get supplementary group list */
#define SYS_SETGROUPS 81     /* Set supplementary group list */
#define SYS_SELECT   82      /* Synchronous I/O multiplexing */
#define SYS_SYMLINK  83      /* Create a symbolic link */
#define SYS_READLINK 85      /* Read a symbolic link */
#define SYS_REBOOT   88      /* Reboot / power off */

/* ---- Memory (90..97) ---- */
#define SYS_MMAP     90     /* Old 32-bit mmap (6 args, offset in bytes) */
#define SYS_MUNMAP   91
#define SYS_TRUNCATE 92     /* Truncate a file by path */
#define SYS_FTRUNCATE 93    /* Truncate an open file */
#define SYS_FCHMOD   94     /* Change permissions of an open fd */
#define SYS_FCHOWN   95     /* Change owner of an open fd */
#define SYS_GETPRIORITY 96  /* Get scheduling priority (nice) */
#define SYS_SETPRIORITY 97  /* Set scheduling priority (nice) */

/* ---- Filesystem stats (99..100) ---- */
#define SYS_STATFS   99     /* Get filesystem statistics by path */
#define SYS_FSTATFS  100    /* Get filesystem statistics by fd */

/* ---- Timers / stat (104..108) ---- */
#define SYS_SETITIMER 104   /* Set an interval timer */
#define SYS_GETITIMER 105   /* Get an interval timer */
#define SYS_STAT     106    /* Get file status by path */
#define SYS_LSTAT    107    /* Stat without following symlinks */
#define SYS_FSTAT    108    /* Get file status by fd */

/* ---- wait4 / sync (114..126) ---- */
#define SYS_WAIT4    114    /* Wait with rusage (4 args) */
#define SYS_FSYNC    118    /* Sync a file to disk */
#define SYS_SIGRETURN 119   /* Return from a signal handler */
#define SYS_SETDOMAINNAME 121 /* Set the NIS domain name */
#define SYS_UNAME    122    /* Get system name information */
#define SYS_MPROTECT 125    /* Change protection on a memory region */
#define SYS_SIGPROCMASK 126 /* Examine/change blocked signals */

/* ---- getpgid / fchdir (132..133) ---- */
#define SYS_GETPGID  132    /* Get process group id */
#define SYS_FCHDIR   133    /* Change working directory by fd */

/* ---- flock (143) ---- */
#define SYS_FLOCK    143    /* Apply/release an advisory lock on an open file */

/* ---- utime (30) ---- */
#define SYS_UTIME    30     /* Set file access/modification time */

/* ---- personality (136) / fs creds (138..139) ---- */
#define SYS_PERSONALITY 136  /* Get/set execution domain */
#define SYS_SETFSUID 138    /* Set filesystem user id */
#define SYS_SETFSGID 139    /* Set filesystem group id */

/* ---- getdents / readv (141..148) ---- */
#define SYS_GETDENTS 141    /* Read directory entries */
#define SYS_READV    145    /* Vector read */
#define SYS_WRITEV   146    /* Vector write */
#define SYS_GETSID   147    /* Get session id */
#define SYS_FDATASYNC 148   /* Sync file data to disk */

/* ---- Memory locking (150..153) ---- */
#define SYS_MLOCK    150
#define SYS_MUNLOCK  151
#define SYS_MLOCKALL 152
#define SYS_MUNLOCKALL 153

/* ---- Scheduling (154..161) ---- */
#define SYS_SCHED_SETPARAM 154
#define SYS_SCHED_GETPARAM 155
#define SYS_SCHED_GETSCHEDULER 157
#define SYS_SCHED_YIELD 158
#define SYS_SCHED_GET_PRIORITY_MAX 159
#define SYS_SCHED_GET_PRIORITY_MIN 160
#define SYS_SCHED_RR_GET_INTERVAL 161

/* ---- poll / prctl (168..172) ---- */
#define SYS_POLL     168    /* Poll fds for readiness */
#define SYS_PRCTL    172    /* Process control (PR_SET_NAME etc.) */

/* ---- pread/pwrite/getcwd (180..183) ---- */
#define SYS_PREAD    180    /* Read at explicit offset */
#define SYS_PWRITE   181    /* Write at explicit offset */
#define SYS_GETCWD   183    /* Get current working directory */

/* ---- mmap2 (192): mmap with page-granular offset (Linux i386 ABI) ---- */
#define SYS_MMAP2    192

/* ---- sendfile (187) ---- */
#define SYS_SENDFILE 187    /* Copy data between file descriptors */

/* ---- fadvise64 (221) ---- */
#define SYS_FADVISE64 221   /* File access pattern hint (no-op) */

/* ---- exit_group / set_tid_address (252..258) ---- */
#define SYS_EXIT_GROUP 252  /* Exit all threads (same as exit for now) */
#define SYS_SET_TID_ADDRESS 258

/* ---- clock_gettime family (264..267) ---- */
#define SYS_CLOCK_GETTIME 265
#define SYS_CLOCK_GETRES 266
#define SYS_CLOCK_NANOSLEEP 267

/* ---- tgkill / utimes (270..271) ---- */
#define SYS_TGKILL   270    /* Kill a thread (same as kill without threads) */
#define SYS_UTIMES   271    /* Set atime/mtime with timevals */

/* ---- setresuid family (164..171) / chown (182) ---- */
#define SYS_SETRESUID 164
#define SYS_GETRESUID 165
#define SYS_SETRESGID 170
#define SYS_GETRESGID 171
#define SYS_CHOWN     182

/* ---- syncfs (306) ---- */
#define SYS_SYNCFS   306

/* ---- getcpu (318) ---- */
#define SYS_GETCPU   318

/* ---- getrandom (355) ---- */
#define SYS_GETRANDOM 355   /* getrandom(buf, buflen, flags) */

/* ---- dup3 / pipe2 (330..331) ---- */
#define SYS_DUP3     330    /* dup2 with O_CLOEXEC support */
#define SYS_PIPE2    331    /* pipe with flags */

/* ---- membarrier (375) ---- */
#define SYS_MEMBARRIER 375

/* ---- close_range (436) ---- */
#define SYS_CLOSE_RANGE 436

/* ---- Custom networking extensions (900+, off the Linux i386 range) ---- */
#define SYS_NET_IFCONFIG 900   /* ifconfig(index, struct net_ifconfig *) */
#define SYS_NET_SEND     901   /* net_send(name, frame, len)             */
#define SYS_NET_RECV     902   /* net_recv(name, buf, buflen, block)     */
#define SYS_NET_SETIP    903   /* net_setip(name, ip, netmask, gw)       */

/* =========================================================================
 * waitpid() options
 * ========================================================================= */

#define WNOHANG   1
#define WUNTRACED 2
#define WCONTINUED 8

/* =========================================================================
 * Time Structures
 * ========================================================================= */

/* POSIX timespec (32-bit i386: both fields are 32-bit longs) */
typedef struct timespec {
    int32_t tv_sec;    /* seconds      */
    int32_t tv_nsec;   /* nanoseconds  */
} timespec_t;

/* POSIX timeval (32-bit i386: both fields are 32-bit longs) */
typedef struct timeval {
    int32_t tv_sec;    /* seconds          */
    int32_t tv_usec;   /* microseconds     */
} timeval_t;

/* =========================================================================
 * utsname (system identification)
 * ========================================================================= */

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

extern struct utsname sys_utsname;   /* global system identity (mutable) */

/* =========================================================================
 * User Pointer Validation
 * ========================================================================= */

/**
 * Validate a user pointer is within user address space
 * @param ptr User pointer to validate
 * @param len Length of memory region
 * @return 1 if valid, 0 if invalid
 */
int valid_user_pointer(const void *ptr, size_t len);

/**
 * Copy data from userspace to kernel space safely
 * @param kernel_dst Kernel buffer (destination)
 * @param user_src User buffer (source)
 * @param n Number of bytes to copy
 * @return 0 on success, -1 if user pointer is invalid
 */
int copy_from_user(void *kernel_dst, const void *user_src, size_t n);

/**
 * Copy data from kernel space to userspace safely
 * @param user_dst User buffer (destination)
 * @param kernel_src Kernel buffer (source)
 * @param n Number of bytes to copy
 * @return 0 on success, -1 if user pointer is invalid
 */
int copy_to_user(void *user_dst, const void *kernel_src, size_t n);

/* =========================================================================
 * System Call Handler
 * ========================================================================= */

/**
 * Main syscall dispatcher - called from isr128 (int 0x80)
 * @param regs Pointer to saved register state
 * @param num Syscall number (from EAX)
 * @param arg1 First argument (from EBX)
 * @param arg2 Second argument (from ECX)
 * @param arg3 Third argument (from EDX)
 * @param arg4 Fourth argument (from ESI)
 * @param arg5 Fifth argument (from EDI)
 * @param arg6 Sixth argument (from EBP; used by 6-arg syscalls like mmap)
 * @return Syscall return value (stored in EAX)
 */
int syscall_handler(registers_t *regs, int num, uint32_t arg1, uint32_t arg2,
                    uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6)
    __attribute__((cdecl));

#endif /* SYSCALL_H */