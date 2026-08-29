#ifndef ARCH_UACCESS_H
#define ARCH_UACCESS_H

#include <stddef.h>

/* =========================================================================
 * User-space memory access – arch-neutral API (x86_64 page-table walk).
 * ========================================================================= */

int valid_user_pointer(const void *ptr, size_t len);
int copy_from_user(void *kernel_dst, const void *user_src, size_t n);
int copy_to_user(void *user_dst, const void *kernel_src, size_t n);

#endif /* ARCH_UACCESS_H */
