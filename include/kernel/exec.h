#ifndef EXEC_H
#define EXEC_H

#include <stdint.h>

/* Forward declaration (kernel/sched.h) */
struct task_struct;

/* ============================================================================
 * Loaded image description (result of load_binary())
 * ============================================================================ */

typedef struct {
    uintptr_t entry;        /* user-space entry point (EIP/RIP)      */
    uintptr_t code_start;   /* lowest loaded address                 */
    uintptr_t code_end;     /* highest loaded address (page-aligned) */
    uintptr_t brk_start;    /* heap start (>= code_end)              */
} exec_image_t;

/* ============================================================================
 * Binary Loader (kernel/elf.c)
 *
 * Primary format:  ELF  (ELF32 on i386, ELF64 on x86_64)
 * Legacy format:   flat (header-less RWX blob at USER_TEXT_START)
 * ============================================================================ */

/**
 * Load an executable image into a task's freshly-cleared user address space.
 * Maps all segments/pages, creates the VMAs and fills *img.
 * @param task       Target task (page tables already initialized)
 * @param fd         Open file descriptor, positioned at offset 0
 * @param file_size  File size in bytes
 * @param img        Out: entry + memory region bounds
 * @return 0 on success, -errno (ENOEXEC/ENOMEM/EIO) on failure
 */
int load_binary(struct task_struct *task, int fd, uint32_t file_size,
                exec_image_t *img);

/**
 * Cheap pre-flight check that the file at fd is a loadable executable.
 * ELF headers are validated; legacy flat images always pass.  Call this
 * BEFORE tearing down the old process image, so a bad executable returns an
 * error without destroying the caller's memory.
 * @return 0 if loadable, -errno otherwise
 */
int exec_format_check(int fd, uint32_t file_size);

/* ============================================================================
 * Functions
 * ============================================================================ */

/**
 * Spawn init process (PID 1) from a binary file
 * Creates a task with proper memory management and adds to scheduler
 * @param path Path to binary file
 * @return 0 on success, -1 on error
 */
int spawn_init(const char *path);

#endif /* EXEC_H */
