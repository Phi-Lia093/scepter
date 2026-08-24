#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "kernel/syscall.h"

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
 * sys_fork - Create a copy of the current process
 * @param regs CPU register state from syscall entry
 * @return Child PID to parent, 0 to child, -1 on error
 */
int sys_fork(struct registers *regs);

/**
 * sys_wait - Wait for any child process to exit
 * @param status_ptr User pointer to store exit status (can be NULL)
 * @return Child PID on success, -1 on error
 */
int sys_wait(int *status_ptr);

/**
 * sys_exec - Replace current process with new program
 * @param path Path to executable file
 * @return -1 on error (does not return on success)
 */
int sys_exec(const char *path);

/**
 * sys_nanosleep - Sleep for a specified duration
 * @param req User pointer to timespec (seconds + nanoseconds)
 * @param rem User pointer to store remaining time (can be NULL)
 * @return 0 on success, -1 on error
 */
int sys_nanosleep(timespec_t *req, timespec_t *rem);

#endif /* PROCESS_H */