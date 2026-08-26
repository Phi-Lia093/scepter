/* ============================================================================
 * System Call Wrapper Library
 * C function wrappers for kernel syscalls via int 0x80.
 *
 * Error convention: the kernel returns a NEGATIVE errno on failure.
 * Each wrapper stores -ret in errno and returns -1 (or the appropriate
 * error sentinel).
 * ============================================================================ */

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
    long ret = syscall4(SYS_WAIT4, (int)pid, (int)status, options, 0);
    if (ret < 0) { errno = -ret; return -1; }
    return (pid_t)ret;
}

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
}

int exec(const char *path)
{
    long ret = syscall1(SYS_EXEC, (int)path);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;   /* never reached on success */
}

int execv(const char *path, char *const argv[])
{
    long ret = syscall2(SYS_EXECV, (int)path, (int)argv);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
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

int open(const char *path, int flags)
{
    long ret = syscall2(SYS_OPEN, (int)path, flags);
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
    (void)offset;   /* the kernel always uses offset 0 */
    long ret = syscall5(SYS_MMAP, (int)addr, (int)length, prot, flags, fd);
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
