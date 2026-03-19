#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * System Call Numbers (Linux-compatible subset)
 * ========================================================================= */

#define SYS_OPEN   1
#define SYS_CLOSE  6
#define SYS_WRITE  4
#define SYS_READ   3

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
 * @param num Syscall number (from EAX)
 * @param arg1 First argument (from EBX)
 * @param arg2 Second argument (from ECX)
 * @param arg3 Third argument (from EDX)
 * @param arg4 Fourth argument (from ESI)
 * @param arg5 Fifth argument (from EDI)
 * @return Syscall return value (stored in EAX)
 */
int syscall_handler(int num, uint32_t arg1, uint32_t arg2,
                    uint32_t arg3, uint32_t arg4, uint32_t arg5)
    __attribute__((cdecl));

#endif /* SYSCALL_H */