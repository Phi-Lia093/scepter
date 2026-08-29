#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "kernel/syscall.h"
#include "fs/fs.h"

/* =========================================================================
 * Executable argument/environment copying
 *
 * The kernel copies argv/envp from the caller's (old) user space into a
 * fixed kernel scratch area BEFORE freeing the old process image. The
 * strings are then packed into the fresh user stack page of the new image
 * by setup_initial_stack().
 * ========================================================================= */

#define EXEC_MAX_ARGS 32    /* max argc/envc entries               */
#define EXEC_MAX_STR  128   /* max bytes per individual string     */
#define EXEC_MAX_DATA 1024  /* max total string bytes per table    */

typedef struct {
    char     *ptrs[EXEC_MAX_ARGS];  /* NULL-terminated string pointers */
    char      data[EXEC_MAX_DATA];  /* packed string storage           */
    uint32_t  count;                /* number of strings               */
    uint32_t  data_len;             /* bytes used in data[]            */
} exec_strings_t;

/**
 * Copy a NULL-terminated array of user pointers to strings into kernel
 * scratch (an exec_strings_t). Empty (NULL) array yields count == 0.
 * @param user_ptrs User pointer to the pointer array (may be NULL)
 * @param out       Kernel scratch to fill
 * @return 0 on success, -1 on invalid pointer / overflow
 */
int copy_exec_strings(char **user_ptrs, exec_strings_t *out);

/**
 * Build the initial user stack (argc/argv[]/envp[]/strings) for a new
 * process image.
 * @param stack_pages  Direct-mapped base of the (2-page) stack allocation
 * @param stack_vaddr  User virtual address of stack_pages (0xBFFFE000)
 * @param argv         Kernel argv table (count >= 1)
 * @param envp         Kernel envp table (may be empty)
 * @param esp          Out: initial user ESP pointing at argc
 * @return 0 on success, -1 if the block does not fit
 */
int setup_initial_stack(void *stack_pages, uintptr_t stack_vaddr,
                        exec_strings_t *argv, exec_strings_t *envp,
                        uintptr_t *esp);

/* =========================================================================
 * Process Management System Calls
 * ========================================================================= */

/**
 * sys_exit - Terminate current process
 * @param status Exit status code
 * 
 * This function never returns.
 */
void sys_exit(int status) __attribute__((noreturn));

/**
 * do_exit - Terminate the current process (shared core for sys_exit and
 * kill-on-fault paths). Never returns.
 * @param status Exit status code
 */
void do_exit(int status) __attribute__((noreturn));

/**
 * sys_fork - Create a copy of the current process
 * @param regs CPU register state from syscall entry
 * @return Child PID to parent, 0 to child, -1 on error
 */
int sys_fork(struct registers *regs);

/**
 * sys_wait4 - Wait for a child process to exit
 * @param pid  -1: any child; 0: any group child; >0: specific child
 * @param status_ptr User pointer to store exit status (can be NULL)
 * @param options    Reserved (0)
 * @param rusage     Reserved (NULL)
 * @return Child PID on success, -1 on error
 */
int sys_wait4(int pid, int *status_ptr, int options, void *rusage);

/**
 * sys_wait - Wait for any child process to exit
 * @param status_ptr User pointer to store exit status (can be NULL)
 * @return Child PID on success, -1 on error
 */
int sys_wait(int *status_ptr);

/**
 * sys_exec - Replace current process with new program (no argv/envp)
 * @param path Path to executable file
 * @return -1 on error (does not return on success)
 */
int sys_exec(const char *path);

/**
 * sys_execv - Replace current process with new program + argv
 * @param path Path to executable file
 * @param argv User pointer to NULL-terminated argv array
 * @return -1 on error (does not return on success)
 */
int sys_execv(const char *path, char **argv);

/**
 * sys_execve - Replace current process with new program + argv + envp
 * @param path Path to executable file
 * @param argv User pointer to NULL-terminated argv array
 * @param envp User pointer to NULL-terminated envp array
 * @return -1 on error (does not return on success)
 */
int sys_execve(const char *path, char **argv, char **envp);

/**
 * sys_chdir - Change the current working directory
 * @param path User pointer to directory path
 * @return 0 on success, -1 on error
 */
int sys_chdir(const char *path);

/**
 * sys_getdents - Read directory entries
 * @param fd   Directory file descriptor
 * @param buf  User buffer for dirent_t array
 * @param count Maximum number of entries to read
 * @return Number of entries filled, 0 at end, -1 on error
 */
int sys_getdents(int fd, dirent_t *buf, unsigned int count);

/**
 * sys_nanosleep - Sleep for a specified duration
 * @param req User pointer to timespec (seconds + nanoseconds)
 * @param rem User pointer to store remaining time (can be NULL)
 * @return 0 on success, -1 on error
 */
int sys_nanosleep(timespec_t *req, timespec_t *rem);

/* =========================================================================
 * Process groups & sessions
 * ========================================================================= */

int sys_setpgid(int pid, int pgid);
int sys_getpgid(int pid);
int sys_getpgrp(void);
int sys_setsid(void);
int sys_getsid(int pid);

/* =========================================================================
 * User / group ids
 * ========================================================================= */

int sys_getuid(void);
int sys_geteuid(void);
int sys_getgid(void);
int sys_getegid(void);
int sys_setuid(uint32_t uid);
int sys_setgid(uint32_t gid);
int sys_umask(uint32_t mask);

#endif /* PROCESS_H */