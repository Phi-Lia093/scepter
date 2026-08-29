/* ============================================================================
 * System Call Wrapper Library
 * C function wrappers for kernel syscalls via int 0x80.
 *
 * Error convention: the kernel returns a NEGATIVE errno on failure.
 * Each wrapper stores -ret in errno and returns -1 (or the appropriate
 * error sentinel).
 * ============================================================================ */

#include <stdarg.h>
#include "syscall.h"
#include "errno.h"
#include "signal.h"
#include "time.h"
#include "sys/stat.h"
#include "sys/wait.h"
#include "sys/ioctl.h"
#include "sys/utsname.h"
#include "sys/mman.h"
#include "sys/uio.h"
#include "sys/select.h"
#include "sys/times.h"
#include "sys/time.h"
#include "utime.h"
#include "termios.h"
#include "sys/mount.h"
#include "poll.h"
#include "sched.h"
#include "sys/vfs.h"
#include "sys/sysinfo.h"
#include "sys/prctl.h"
#include "sys/reboot.h"
#include "sys/net.h"
#include "dirent.h"
#include "fcntl.h"

/* ============================================================================
 * Syscall invocation helpers (int 0x80, up to 5 args)
 * Arguments: EAX=num EBX=arg1 ECX=arg2 EDX=arg3 ESI=arg4 EDI=arg5
 * ============================================================================ */

#define INLINE __attribute__((always_inline)) static inline

INLINE int syscall0(int num)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory", "ebx", "ecx", "edx", "esi", "edi", "ebp"
    );
    return ret;
}

INLINE int syscall1(int num, int arg1)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
        : "memory", "ecx", "edx", "esi", "edi", "ebp"
    );
    return ret;
}

INLINE int syscall2(int num, int arg1, int arg2)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2)
        : "memory", "edx", "esi", "edi", "ebp"
    );
    return ret;
}

INLINE int syscall3(int num, int arg1, int arg2, int arg3)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory", "esi", "edi", "ebp"
    );
    return ret;
}

INLINE int syscall4(int num, int arg1, int arg2, int arg3, int arg4)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4)
        : "memory", "edi", "ebp"
    );
    return ret;
}

INLINE int syscall5(int num, int arg1, int arg2, int arg3, int arg4, int arg5)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4), "D"(arg5)
        : "memory", "ebp"
    );
    return ret;
}

/* 6-arg syscalls pass the 6th argument in EBP (Linux i386 convention). */
INLINE int syscall6(int num, int arg1, int arg2, int arg3, int arg4,
                    int arg5, int arg6)
{
    int ret;
    __asm__ volatile(
        "movl %7, %%ebp\n\t"
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4), "D"(arg5),
          "m"(arg6)
        : "memory", "ebp"
    );
    return ret;
}

/* ============================================================================
 * Process control
 * ============================================================================ */

void exit(int status) __attribute__((noreturn));
void exit(int status)
{
    syscall1(SYS_EXIT, status);
    /* Never returns */
    for (;;) {}
}

void _exit(int status) __attribute__((noreturn));
void _exit(int status)
{
    exit(status);
}

pid_t fork(void)
{
    long ret = syscall0(SYS_FORK);
    if (ret < 0) { errno = -ret; return -1; }
    return (pid_t)ret;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    long ret = syscall3(SYS_WAITPID, (int)pid, (int)status, options);
    if (ret < 0) { errno = -ret; return -1; }
    return (pid_t)ret;
}

pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage)
{
    long ret = syscall4(SYS_WAIT4, (int)pid, (int)status, options,
                        (int)rusage);
    if (ret < 0) { errno = -ret; return -1; }
    return (pid_t)ret;
}

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
}

int exec(const char *path)
{
    /* exec(path) == execve(path, [path, NULL], environ) */
    char *argv[] = { (char *)path, NULL };
    extern char **environ;
    return execve(path, argv, environ);
}

int execv(const char *path, char *const argv[])
{
    extern char **environ;
    return execve(path, argv, environ);
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    long ret = syscall3(SYS_EXECVE, (int)path, (int)argv, (int)envp);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

pid_t getpid(void)
{
    return (pid_t)syscall0(SYS_GETPID);
}

pid_t getppid(void)
{
    return (pid_t)syscall0(SYS_GETPPID);
}

int nice(int inc)
{
    long ret = syscall1(SYS_NICE, inc);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

/* ============================================================================
 * User / group ids
 * ============================================================================ */

uid_t getuid(void)
{
    long ret = syscall0(SYS_GETUID);
    if (ret < 0) { errno = -ret; return (uid_t)-1; }
    return (uid_t)ret;
}

uid_t geteuid(void)
{
    long ret = syscall0(SYS_GETEUID);
    if (ret < 0) { errno = -ret; return (uid_t)-1; }
    return (uid_t)ret;
}

gid_t getgid(void)
{
    long ret = syscall0(SYS_GETGID);
    if (ret < 0) { errno = -ret; return (gid_t)-1; }
    return (gid_t)ret;
}

gid_t getegid(void)
{
    long ret = syscall0(SYS_GETEGID);
    if (ret < 0) { errno = -ret; return (gid_t)-1; }
    return (gid_t)ret;
}

int setuid(uid_t uid)
{
    long ret = syscall1(SYS_SETUID, (int)uid);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int setgid(gid_t gid)
{
    long ret = syscall1(SYS_SETGID, (int)gid);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ============================================================================
 * Process groups / sessions
 * ============================================================================ */

int setpgid(pid_t pid, pid_t pgid)
{
    long ret = syscall2(SYS_SETPGID, (int)pid, (int)pgid);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

pid_t getpgid(pid_t pid)
{
    long ret = syscall1(SYS_GETPGID, (int)pid);
    if (ret < 0) { errno = -ret; return -1; }
    return (pid_t)ret;
}

pid_t getpgrp(void)
{
    long ret = syscall0(SYS_GETPGRP);
    if (ret < 0) { errno = -ret; return -1; }
    return (pid_t)ret;
}

pid_t setsid(void)
{
    long ret = syscall0(SYS_SETSID);
    if (ret < 0) { errno = -ret; return -1; }
    return (pid_t)ret;
}

pid_t getsid(pid_t pid)
{
    long ret = syscall1(SYS_GETSID, (int)pid);
    if (ret < 0) { errno = -ret; return -1; }
    return (pid_t)ret;
}

/* ============================================================================
 * File I/O
 * ============================================================================ */

int open_mode(const char *path, int flags, unsigned int mode)
{
    long ret = syscall3(SYS_OPEN, (int)path, flags, (int)mode);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

/* POSIX open() is variadic: the mode is only meaningful with O_CREAT. */
int open(const char *path, int flags, ...)
{
    unsigned int mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, unsigned int);
        va_end(ap);
    }
    return open_mode(path, flags, mode);
}

/* creat(path, mode) == open(path, O_CREAT | O_WRONLY | O_TRUNC, mode) */
int creat(const char *path, unsigned int mode)
{
    long ret = syscall2(SYS_CREAT, (int)path, (int)mode);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

ssize_t read(int fd, void *buf, size_t count)
{
    long ret = syscall3(SYS_READ, fd, (int)buf, (int)count);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    long ret = syscall3(SYS_WRITE, fd, (int)buf, (int)count);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

int close(int fd)
{
    long ret = syscall1(SYS_CLOSE, fd);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int fcntl(int fd, int cmd, ...)
{
    uint32_t arg = 0;
    __builtin_va_list ap;
    __builtin_va_start(ap, cmd);
    arg = __builtin_va_arg(ap, uint32_t);
    __builtin_va_end(ap);

    long ret = syscall3(SYS_FCNTL, fd, cmd, (int)arg);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int dup3(int oldfd, int newfd, int flags)
{
    long ret = syscall3(SYS_DUP3, oldfd, newfd, flags);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

off_t lseek(int fd, off_t offset, int whence)
{
    long ret = syscall3(SYS_LSEEK, fd, (int)offset, whence);
    if (ret < 0) { errno = -ret; return -1; }
    return (off_t)ret;
}

int dup(int oldfd)
{
    long ret = syscall1(SYS_DUP, oldfd);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int dup2(int oldfd, int newfd)
{
    long ret = syscall2(SYS_DUP2, oldfd, newfd);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int pipe(int fds[2])
{
    long ret = syscall1(SYS_PIPE, (int)fds);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int ioctl(int fd, unsigned int cmd, unsigned int arg)
{
    long ret = syscall3(SYS_IOCTL, fd, (int)cmd, (int)arg);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int truncate(const char *path, off_t length)
{
    long ret = syscall2(SYS_TRUNCATE, (int)path, (int)length);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

/* ============================================================================
 * Directory operations
 * ============================================================================ */

char *getcwd(char *buf, size_t size)
{
    long ret = syscall2(SYS_GETCWD, (int)buf, (int)size);
    if (ret < 0) { errno = -ret; return NULL; }
    return buf;
}

int chdir(const char *path)
{
    long ret = syscall1(SYS_CHDIR, (int)path);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int mkdir(const char *path, mode_t mode)
{
    long ret = syscall2(SYS_MKDIR, (int)path, (int)mode);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int rmdir(const char *path)
{
    long ret = syscall1(SYS_RMDIR, (int)path);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int unlink(const char *path)
{
    long ret = syscall1(SYS_UNLINK, (int)path);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int rename(const char *oldpath, const char *newpath)
{
    long ret = syscall2(SYS_RENAME, (int)oldpath, (int)newpath);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int getdents(int fd, struct dirent *buf, unsigned int count)
{
    long ret = syscall3(SYS_GETDENTS, fd, (int)buf, (int)count);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int access(const char *path, int mode)
{
    long ret = syscall2(SYS_ACCESS, (int)path, mode);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int isatty(int fd)
{
    struct stat st;
    if (fstat(fd, &st) < 0)
        return 0;
    return st.st_type == DT_CHRDEV;
}

/* ============================================================================
 * Metadata
 * ============================================================================ */

int stat(const char *path, struct stat *buf)
{
    long ret = syscall2(SYS_STAT, (int)path, (int)buf);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int fstat(int fd, struct stat *buf)
{
    long ret = syscall2(SYS_FSTAT, fd, (int)buf);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int uname(struct utsname *buf)
{
    long ret = syscall1(SYS_UNAME, (int)buf);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ============================================================================
 * Extended file I/O (Phase B)
 * ============================================================================ */

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    long ret = syscall4(SYS_PREAD, fd, (int)buf, (int)count, (int)offset);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    long ret = syscall4(SYS_PWRITE, fd, (int)buf, (int)count, (int)offset);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

int ftruncate(int fd, off_t length)
{
    long ret = syscall2(SYS_FTRUNCATE, fd, (int)length);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int fsync(int fd)
{
    long ret = syscall1(SYS_FSYNC, fd);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int fdatasync(int fd)
{
    long ret = syscall1(SYS_FDATASYNC, fd);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    long ret = syscall3(SYS_READV, fd, (int)iov, iovcnt);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    long ret = syscall3(SYS_WRITEV, fd, (int)iov, iovcnt);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

/* ============================================================================
 * Links / metadata (Phase B)
 * ============================================================================ */

int link(const char *oldpath, const char *newpath)
{
    long ret = syscall2(SYS_LINK, (int)oldpath, (int)newpath);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int symlink(const char *target, const char *linkpath)
{
    long ret = syscall2(SYS_SYMLINK, (int)target, (int)linkpath);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

ssize_t readlink(const char *path, char *buf, size_t bufsize)
{
    long ret = syscall3(SYS_READLINK, (int)path, (int)buf, (int)bufsize);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

int lstat(const char *path, struct stat *buf)
{
    long ret = syscall2(SYS_LSTAT, (int)path, (int)buf);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int chmod(const char *path, mode_t mode)
{
    long ret = syscall2(SYS_CHMOD, (int)path, (int)mode);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int fchmod(int fd, mode_t mode)
{
    long ret = syscall2(SYS_FCHMOD, fd, (int)mode);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int mknod(const char *path, mode_t mode, int dev)
{
    long ret = syscall3(SYS_MKNOD, (int)path, (int)mode, dev);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

mode_t umask(mode_t mask)
{
    long ret = syscall1(SYS_UMASK, (int)mask);
    if (ret < 0) { errno = -ret; return 0; }
    return (mode_t)ret;
}

/* ============================================================================
 * select() / poll()
 * ============================================================================ */

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    long ret = syscall3(SYS_POLL, (int)fds, (int)nfds, timeout);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
    long ret = syscall5(SYS_SELECT, nfds, (int)readfds, (int)writefds,
                        (int)exceptfds, (int)timeout);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

/* ============================================================================
 * Memory
 * ============================================================================ */

int brk(void *addr)
{
    long ret = syscall1(SYS_BRK, (int)addr);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset)
{
    long ret = syscall6(SYS_MMAP, (int)addr, (int)length, prot, flags, fd,
                        (int)offset);
    if (ret < 0) { errno = -ret; return MAP_FAILED; }
    return (void *)ret;
}

/* Linux i386 mmap2: the offset is in units of 4096-byte pages. */
void *mmap2(void *addr, size_t length, int prot, int flags, int fd,
            size_t pgoffset)
{
    long ret = syscall6(SYS_MMAP2, (int)addr, (int)length, prot, flags, fd,
                        (int)pgoffset);
    if (ret < 0) { errno = -ret; return MAP_FAILED; }
    return (void *)ret;
}

int munmap(void *addr, size_t length)
{
    long ret = syscall2(SYS_MUNMAP, (int)addr, (int)length);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int mprotect(void *addr, size_t length, int prot)
{
    long ret = syscall3(SYS_MPROTECT, (int)addr, (int)length, prot);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ============================================================================
 * Time
 * ============================================================================ */

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    long ret = syscall2(SYS_NANOSLEEP, (int)req, (int)rem);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    long ret = syscall2(SYS_GETTIMEOFDAY, (int)tv, (int)tz);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

unsigned int sleep(unsigned int seconds)
{
    struct timespec req = { (long)seconds, 0 };
    struct timespec rem;
    if (nanosleep(&req, &rem) < 0)
        return (unsigned int)rem.tv_sec;
    return 0;
}

int usleep(unsigned int usec)
{
    struct timespec req = { (long)(usec / 1000000), (long)((usec % 1000000) * 1000) };
    return nanosleep(&req, NULL);
}

time_t time(time_t *tloc)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
        return (time_t)-1;
    if (tloc)
        *tloc = (time_t)ts.tv_sec;
    return (time_t)ts.tv_sec;
}

int clock_gettime(clockid_t clockid, struct timespec *ts)
{
    long ret = syscall2(SYS_CLOCK_GETTIME, (int)clockid, (int)ts);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int clock_getres(clockid_t clockid, struct timespec *ts)
{
    long ret = syscall2(SYS_CLOCK_GETRES, (int)clockid, (int)ts);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

unsigned int alarm(unsigned int seconds)
{
    long ret = syscall1(SYS_ALARM, (int)seconds);
    if (ret < 0) { errno = -ret; return 0; }
    return (unsigned int)ret;
}

int setitimer(int which, const struct itimerval *new_value,
              struct itimerval *old_value)
{
    long ret = syscall3(SYS_SETITIMER, which, (int)new_value, (int)old_value);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int getitimer(int which, struct itimerval *value)
{
    long ret = syscall2(SYS_GETITIMER, which, (int)value);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int utime(const char *path, const struct utimbuf *times)
{
    long ret = syscall2(SYS_UTIME, (int)path, (int)times);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ============================================================================
 * termios
 * ============================================================================ */

int tcgetattr(int fd, struct termios *t)
{
    long ret = syscall3(SYS_IOCTL, fd, TCGETS, (int)t);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *t)
{
    unsigned int cmd = TCSETS;
    if (optional_actions == TCSADRAIN)
        cmd = TCSETSW;
    else if (optional_actions == TCSAFLUSH)
        cmd = TCSETSF;
    long ret = syscall3(SYS_IOCTL, fd, cmd, (int)t);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int tcflow(int fd, int action)
{
    (void)action;
    long ret = syscall3(SYS_IOCTL, fd, TCSETSW, (int)0);
    (void)ret;
    errno = ENOTTY;
    return -1;   /* not implemented */
}

int tcflush(int fd, int queue_selector)
{
    if (queue_selector == TCIFLUSH) {
        /* TCSETSF with the current settings flushes input. */
        struct termios t;
        if (tcgetattr(fd, &t) == 0)
            return tcsetattr(fd, TCSAFLUSH, &t);
        return -1;
    }
    errno = EINVAL;
    return -1;
}

int tcsendbreak(int fd, int duration)
{
    (void)fd; (void)duration;
    return 0;
}

void cfmakeraw(struct termios *t)
{
    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                    ICRNL | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN]  = 1;
    t->c_cc[VTIME] = 0;
}

speed_t cfgetispeed(const struct termios *t) { (void)t; return 0; }
speed_t cfgetospeed(const struct termios *t) { (void)t; return 0; }

/* ============================================================================
 * Filesystem mount/umount/sync
 * ============================================================================ */

int mount(const char *source, const char *target,
          const char *filesystemtype, unsigned long mountflags,
          const void *data)
{
    long ret = syscall5(SYS_MOUNT, (int)source, (int)target,
                        (int)filesystemtype, (int)mountflags, (int)data);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int umount(const char *target)
{
    long ret = syscall1(SYS_UMOUNT, (int)target);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int umount2(const char *target, int flags)
{
    (void)flags;
    return umount(target);
}

void sync(void)
{
    (void)syscall0(SYS_SYNC);
}

clock_t times(struct tms *buf)
{
    long ret = syscall1(SYS_TIMES, (int)buf);
    if (ret < 0) { errno = -ret; return (clock_t)-1; }
    return (clock_t)ret;
}

/* ============================================================================
 * Signals
 * ============================================================================ */

sighandler_t signal(int signum, sighandler_t handler)
{
    long ret = syscall2(SYS_SIGNAL, signum, (int)handler);
    if (ret < 0) { errno = -ret; return SIG_ERR; }
    return (sighandler_t)ret;
}

int kill(pid_t pid, int sig)
{
    long ret = syscall2(SYS_KILL, (int)pid, sig);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int raise(int sig)
{
    return kill(getpid(), sig);
}

int killpg(int pgrp, int sig)
{
    return kill(-pgrp, sig);
}

int sigaction(int signum, const struct sigaction *act,
              struct sigaction *oldact)
{
    long ret = syscall3(SYS_SIGACTION, signum, (int)act, (int)oldact);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    long ret = syscall3(SYS_SIGPROCMASK, how, (int)set, (int)oldset);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int sigpending(sigset_t *set)
{
    long ret = syscall1(SYS_SIGPENDING, (int)set);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int sigsuspend(const sigset_t *mask)
{
    long ret = syscall1(SYS_SIGSUSPEND, (int)mask);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* Suspend until a signal is caught; implemented via sigsuspend(). */
int pause(void)
{
    sigset_t empty;
    sigemptyset(&empty);
    return sigsuspend(&empty);
}

/* Called by the kernel signal trampoline; restores the interrupted context. */
int sigreturn(void)
{
    return syscall0(SYS_SIGRETURN);
}

/* ============================================================================
 * Tier 1-3 syscall wrappers (added with the Linux-number alignment)
 * ============================================================================ */

/* ---- Scheduling ---- */

int sched_yield(void)
{
    return (int)syscall0(SYS_SCHED_YIELD);
}

int sched_getparam(pid_t pid, struct sched_param *param)
{
    long ret = syscall2(SYS_SCHED_GETPARAM, (int)pid, (int)param);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int sched_setparam(pid_t pid, const struct sched_param *param)
{
    long ret = syscall2(SYS_SCHED_SETPARAM, (int)pid, (int)param);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int sched_getscheduler(pid_t pid)
{
    long ret = syscall1(SYS_SCHED_GETSCHEDULER, (int)pid);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int sched_get_priority_max(int policy)
{
    long ret = syscall1(SYS_SCHED_GET_PRIORITY_MAX, policy);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int sched_get_priority_min(int policy)
{
    long ret = syscall1(SYS_SCHED_GET_PRIORITY_MIN, policy);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int sched_rr_get_interval(pid_t pid, struct timespec *tp)
{
    long ret = syscall2(SYS_SCHED_RR_GET_INTERVAL, (int)pid, (int)tp);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int getpriority(int which, int who)
{
    long ret = syscall2(SYS_GETPRIORITY, which, who);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int setpriority(int which, int who, int niceval)
{
    long ret = syscall3(SYS_SETPRIORITY, which, who, niceval);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ---- Process ---- */

void exit_group(int status)
{
    syscall1(SYS_EXIT_GROUP, status);
    for (;;) __asm__ volatile("hlt");
}

int tgkill(int tgid, int tid, int sig)
{
    long ret = syscall3(SYS_TGKILL, tgid, tid, sig);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int set_tid_address(int *tidptr)
{
    return (int)syscall1(SYS_SET_TID_ADDRESS, (int)tidptr);
}

int prctl(int option, unsigned long arg2, unsigned long arg3,
          unsigned long arg4, unsigned long arg5)
{
    long ret = syscall5(SYS_PRCTL, option, (int)arg2, (int)arg3,
                        (int)arg4, (int)arg5);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int personality(unsigned long persona)
{
    long ret = syscall1(SYS_PERSONALITY, (int)persona);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

/* ---- File descriptors ---- */

int pipe2(int pipefd[2], int flags)
{
    long ret = syscall2(SYS_PIPE2, (int)pipefd, flags);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int close_range(unsigned int first, unsigned int last, int flags)
{
    long ret = syscall3(SYS_CLOSE_RANGE, (int)first, (int)last, flags);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int fchdir(int fd)
{
    long ret = syscall1(SYS_FCHDIR, fd);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
    long ret = syscall4(SYS_SENDFILE, out_fd, in_fd, (int)offset,
                        (int)count);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

/* ---- Filesystem ---- */

int statfs(const char *path, struct statfs *buf)
{
    long ret = syscall2(SYS_STATFS, (int)path, (int)buf);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int fstatfs(int fd, struct statfs *buf)
{
    long ret = syscall2(SYS_FSTATFS, fd, (int)buf);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int syncfs(int fd)
{
    long ret = syscall1(SYS_SYNCFS, fd);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int fadvise64(int fd, off_t offset, off_t len, int advice)
{
    long ret = syscall4(SYS_FADVISE64, fd, (int)offset, (int)len, advice);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
    return fadvise64(fd, offset, len, advice);
}

/* ---- System info / resources ---- */

int sysinfo(struct sysinfo *info)
{
    long ret = syscall1(SYS_SYSINFO, (int)info);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* getrandom(2): fill buf with random bytes.  Never blocks in Scepter. */
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags)
{
    long ret = syscall3(SYS_GETRANDOM, (int)buf, (int)buflen, (int)flags);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

int getrusage(int who, struct rusage *usage)
{
    long ret = syscall2(SYS_GETRUSAGE, who, (int)usage);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int getrlimit(int resource, struct rlimit *rlim)
{
    long ret = syscall2(SYS_GETRLIMIT, resource, (int)rlim);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim)
{
    long ret = syscall2(SYS_SETRLIMIT, resource, (int)rlim);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ---- Hostname / reboot ---- */

int sethostname(const char *name, size_t len)
{
    long ret = syscall2(SYS_SETHOSTNAME, (int)name, (int)len);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int setdomainname(const char *name, size_t len)
{
    long ret = syscall2(SYS_SETDOMAINNAME, (int)name, (int)len);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int reboot(int magic1, int magic2, int cmd, void *arg)
{
    long ret = syscall4(SYS_REBOOT, magic1, magic2, cmd, (int)arg);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ---- Credentials ---- */

int setreuid(uid_t ruid, uid_t euid)
{
    long ret = syscall2(SYS_SETREUID, (int)ruid, (int)euid);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int setregid(gid_t rgid, gid_t egid)
{
    long ret = syscall2(SYS_SETREGID, (int)rgid, (int)egid);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
    long ret = syscall3(SYS_SETRESUID, (int)ruid, (int)euid, (int)suid);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
    long ret = syscall3(SYS_GETRESUID, (int)ruid, (int)euid, (int)suid);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
    long ret = syscall3(SYS_SETRESGID, (int)rgid, (int)egid, (int)sgid);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
    long ret = syscall3(SYS_GETRESGID, (int)rgid, (int)egid, (int)sgid);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int setfsuid(uid_t fsuid)
{
    return (int)syscall1(SYS_SETFSUID, (int)fsuid);
}

int setfsgid(gid_t fsgid)
{
    return (int)syscall1(SYS_SETFSGID, (int)fsgid);
}

int getgroups(int size, gid_t list[])
{
    long ret = syscall2(SYS_GETGROUPS, size, (int)list);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int setgroups(int size, const gid_t list[])
{
    long ret = syscall2(SYS_SETGROUPS, size, (int)list);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int chown(const char *path, uid_t owner, gid_t group)
{
    long ret = syscall3(SYS_CHOWN, (int)path, (int)owner, (int)group);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int lchown(const char *path, uid_t owner, gid_t group)
{
    long ret = syscall3(SYS_LCHOWN, (int)path, (int)owner, (int)group);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int fchown(int fd, uid_t owner, gid_t group)
{
    long ret = syscall3(SYS_FCHOWN, fd, (int)owner, (int)group);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ---- Memory locking / barriers / cpu ---- */

int mlock(const void *addr, size_t len)
{
    long ret = syscall2(SYS_MLOCK, (int)addr, (int)len);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int munlock(const void *addr, size_t len)
{
    long ret = syscall2(SYS_MUNLOCK, (int)addr, (int)len);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int mlockall(int flags)
{
    long ret = syscall1(SYS_MLOCKALL, flags);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int munlockall(void)
{
    long ret = syscall0(SYS_MUNLOCKALL);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int membarrier(int cmd, int flags)
{
    long ret = syscall2(SYS_MEMBARRIER, cmd, flags);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int getcpu(unsigned *cpu, unsigned *node)
{
    long ret = syscall3(SYS_GETCPU, (int)cpu, (int)node, 0);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ---- Time ---- */

int clock_nanosleep(clockid_t clockid, int flags,
                    const struct timespec *rqtp, struct timespec *rmtp)
{
    long ret = syscall4(SYS_CLOCK_NANOSLEEP, (int)clockid, flags,
                        (int)rqtp, (int)rmtp);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int utimes(const char *path, const struct timeval times[2])
{
    long ret = syscall2(SYS_UTIMES, (int)path, (int)times);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int settimeofday(const struct timeval *tv, const void *tz)
{
    long ret = syscall2(SYS_SETTIMEOFDAY, (int)tv, (int)tz);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int chroot(const char *path)
{
    long ret = syscall1(SYS_CHROOT, (int)path);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int flock(int fd, int operation)
{
    long ret = syscall2(SYS_FLOCK, fd, operation);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

/* ---- Networking (custom SYS_NET_* extensions) ---- */

int net_ifconfig(int index, net_ifconfig_t *out)
{
    long ret = syscall2(SYS_NET_IFCONFIG, index, (int)out);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int net_send(const char *name, const void *frame, unsigned int len)
{
    long ret = syscall3(SYS_NET_SEND, (int)name, (int)frame, (int)len);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int net_recv(const char *name, void *buf, unsigned int buflen, int block)
{
    long ret = syscall4(SYS_NET_RECV, (int)name, (int)buf,
                        (int)buflen, block);
    if (ret < 0) { errno = -ret; return -1; }
    return (int)ret;
}

int net_setip(const char *name, const void *ip,
              const void *netmask, const void *gw)
{
    long ret = syscall4(SYS_NET_SETIP, (int)name, (int)ip,
                        (int)netmask, (int)gw);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}
