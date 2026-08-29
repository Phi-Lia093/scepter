#ifndef ARCH_UACCESS_H
#define ARCH_UACCESS_H

#include <stddef.h>

/* =========================================================================
 * User-space memory access – arch-neutral API.
 *
 * Implemented by the active arch (walks the current task's page tables).
 * ========================================================================= */

/**
 * Validate a user pointer is within user address space.
 * @return 1 if valid, 0 if invalid
 */
int valid_user_pointer(const void *ptr, size_t len);

/**
 * Copy data from userspace to kernel space safely.
 * @return 0 on success, -1 if user pointer is invalid
 */
int copy_from_user(void *kernel_dst, const void *user_src, size_t n);

/**
 * Copy data from kernel space to userspace safely.
 * @return 0 on success, -1 if user pointer is invalid
 */
int copy_to_user(void *user_dst, const void *kernel_src, size_t n);

#endif /* ARCH_UACCESS_H */
