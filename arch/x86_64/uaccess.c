/* ============================================================================
 * User-space memory access (x86_64 page-table walk)
 * ============================================================================ */

#include "arch/paging.h"
#include "arch/uaccess.h"
#include "kernel/sched.h"
#include "lib/string.h"
#include <stddef.h>

#define PTE_PRESENT  0x001
#define PTE_WRITABLE 0x002
#define PTE_HUGE     0x080

/* Walk the current task's PML4 to check [ptr, ptr+len) is mapped. */
static int check_user_range(const void *ptr, size_t len, int need_write)
{
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t end  = addr + len;

    if (end < addr)
        return 0;
    if (addr >= 0x400000000000ULL)      /* user space only */
        return 0;
    if (end > 0x400000000000ULL)
        return 0;
    if (len == 0)
        return 1;

    task_struct_t *task = current;
    if (!task)
        return 0;

    uint64_t *pml4 = task->mm.arch.pml4;
    if (!pml4)
        return 0;

    for (uintptr_t a = addr; a < end; a += 0x1000) {
        uint64_t *e = &pml4[(a >> 39) & 0x1FF];
        if (!(*e & PTE_PRESENT)) return 0;
        uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(*e & ~0xFFFULL);
        e = &pdpt[(a >> 30) & 0x1FF];
        if (!(*e & PTE_PRESENT)) return 0;
        uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(*e & ~0xFFFULL);
        e = &pd[(a >> 21) & 0x1FF];
        if (!(*e & PTE_PRESENT)) return 0;
        if (*e & PTE_HUGE)
            continue;
        uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(*e & ~0xFFFULL);
        uint64_t pte = pt[(a >> 12) & 0x1FF];
        if (!(pte & PTE_PRESENT)) return 0;
        if (need_write && !(pte & PTE_WRITABLE)) return 0;
    }
    return 1;
}

int valid_user_pointer(const void *ptr, size_t len)
{
    return check_user_range(ptr, len, 0);
}

int copy_from_user(void *kernel_dst, const void *user_src, size_t n)
{
    if (!valid_user_pointer(user_src, n))
        return -1;
    memcpy(kernel_dst, user_src, n);
    return 0;
}

int copy_to_user(void *user_dst, const void *kernel_src, size_t n)
{
    if (!check_user_range(user_dst, n, 1))
        return -1;
    memcpy(user_dst, kernel_src, n);
    return 0;
}
