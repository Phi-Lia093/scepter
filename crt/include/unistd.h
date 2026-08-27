#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <sys/types.h>

/* Standard file descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Seek whence values */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Process identification */
pid_t getpid(void);
pid_t getppid(void);

/* User / group identification */
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);

/* Process groups / sessions */
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgid(pid_t pid);
pid_t getpgrp(void);
pid_t setsid(void);
pid_t getsid(pid_t pid);

/* File operations */
int open(const char *path, int flags, ...);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int dup3(int oldfd, int newfd, int flags);
int pipe(int fds[2]);

/* Vector / positional I/O */
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
int ftruncate(int fd, off_t length);
int fsync(int fd);
int fdatasync(int fd);

/* Links */
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsize);
int chmod(const char *path, mode_t mode);
int mknod(const char *path, mode_t mode, int dev);

/* Directory operations */
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int mkdir(const char *path, mode_t mode);
int rmdir(const char *path);
int unlink(const char *path);
int rename(const char *oldpath, const char *newpath);
int truncate(const char *path, off_t length);

/* Process control */
pid_t fork(void);
int exec(const char *path);
int execv(const char *path, char *const argv[]);
int execve(const char *path, char *const argv[], char *const envp[]);
void exit(int status) __attribute__((noreturn));
void _exit(int status) __attribute__((noreturn));

/* File status */
int access(const char *path, int mode);
int isatty(int fd);

/* Sleep */
unsigned int sleep(unsigned int seconds);
int pause(void);

/* Timer */
unsigned int alarm(unsigned int seconds);

/* exec family */
int execl(const char *path, const char *arg0, ...);
int execlp(const char *file, const char *arg0, ...);
int execvp(const char *file, char *const argv[]);

/* Hostname */
int gethostname(char *name, size_t len);
int sethostname(const char *name, size_t len);

/* System configuration */
#define _SC_CLK_TCK          2
#define _SC_OPEN_MAX         4
#define _SC_PAGESIZE         30
#define _SC_NPROCESSORS_ONLN 84
#define _SC_PHYS_PAGES       85
#define _SC_ARG_MAX          0
#define _SC_CHILD_MAX        1

long sysconf(int name);

/* Flush pending disk writes to stable storage. */
void sync(void);

/* Path configuration */
#define _PC_LINK_MAX  0
#define _PC_NAME_MAX  3
#define _PC_PATH_MAX  4

long pathconf(const char *path, int name);

/* getopt */
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
int getopt(int argc, char *const argv[], const char *optstring);

#endif /* _UNISTD_H */

/* ---- extra POSIX / Linux functions ---- */
int fchdir(int fd);
int close_range(unsigned int first, unsigned int last, int flags);
int syncfs(int fd);
int set_tid_address(int *tidptr);
int getcpu(unsigned *cpu, unsigned *node);
int personality(unsigned long persona);
int getgroups(int size, gid_t list[]);
int setgroups(int size, const gid_t list[]);
void exit_group(int status);

/* ---- owners ---- */
int chown(const char *path, uid_t owner, gid_t group);
int lchown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);

/* ---- hostname ---- */
int gethostname(char *name, size_t len);
int sethostname(const char *name, size_t len);
int setdomainname(const char *name, size_t len);

int setreuid(uid_t ruid, uid_t euid);
int setregid(gid_t rgid, gid_t egid);
int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);
int setresuid(uid_t ruid, uid_t euid, uid_t suid);
int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid);
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
int setfsuid(uid_t fsuid);
int setfsgid(gid_t fsgid);
ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
int membarrier(int cmd, int flags);
int fadvise64(int fd, off_t offset, off_t len, int advice);
