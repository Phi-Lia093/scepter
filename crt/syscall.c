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
#include "dirent.h"

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

/* Called by the kernel signal trampoline; restores the interrupted context. */
int sigreturn(void)
{
    return syscall0(SYS_SIGRETURN);
}
