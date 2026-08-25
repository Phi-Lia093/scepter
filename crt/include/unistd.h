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

/* File operations */
int open(const char *path, int flags);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int pipe(int fds[2]);

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

/* File status */
int access(const char *path, int mode);
int isatty(int fd);

/* Sleep */
unsigned int sleep(unsigned int seconds);

#endif /* _UNISTD_H */
