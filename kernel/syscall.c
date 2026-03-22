/* ============================================================================
 * System Call Implementation
 * ============================================================================ */

#include "kernel/syscall.h"
#include "kernel/sched.h"
#include "kernel/process.h"
#include "mm/vma.h"
#include "mm/pgtable.h"
#include "mm/slab.h"
#include "fs/fs.h"
#include "lib/printk.h"
#include "lib/string.h"

/* Kernel virtual memory starts at 3GB */
#define KERNEL_VMA 0xC0000000

/* ============================================================================
 * User Pointer Validation
 * ============================================================================ */

/**
 * Validate a user pointer is within user address space
 * User space: 0x00000000 - 0xBFFFFFFF
 * Kernel space: 0xC0000000 - 0xFFFFFFFF
 */
int valid_user_pointer(const void *ptr, size_t len)
{
    uint32_t addr = (uint32_t)ptr;
    uint32_t end = addr + len;
    
    /* Check for wraparound */
    if (end < addr) {
        return 0;
    }
    
    /* Must be below kernel space */
    if (addr >= KERNEL_VMA) {
        return 0;
    }
    
    if (end > KERNEL_VMA) {
        return 0;
    }
    
    /* TODO: Also check if actually mapped in user page tables */
    
    return 1;
}

/**
 * Copy data from userspace to kernel space safely
 */
int copy_from_user(void *kernel_dst, const void *user_src, size_t n)
{
    /* Validate user pointer */
    if (!valid_user_pointer(user_src, n)) {
        printk("[SYSCALL] Invalid user pointer: 0x%08x (len=%u)\n",
               (uint32_t)user_src, n);
        return -1;
    }
    
    /* Copy data */
    memcpy(kernel_dst, user_src, n);
    return 0;
}

/**
 * Copy data from kernel space to userspace safely
 */
int copy_to_user(void *user_dst, const void *kernel_src, size_t n)
{
    /* Validate user pointer */
    if (!valid_user_pointer(user_dst, n)) {
        printk("[SYSCALL] Invalid user pointer: 0x%08x (len=%u)\n",
               (uint32_t)user_dst, n);
        return -1;
    }
    
    /* Copy data */
    memcpy(user_dst, kernel_src, n);
    return 0;
}

/* ============================================================================
 * System Call Handlers
 * ============================================================================ */

/**
 * sys_open - Open a file
 * @param user_path User pointer to path string
 * @param flags Open flags (O_RDONLY, O_WRONLY, etc.)
 * @return File descriptor on success, -1 on error
 */
static int sys_open(const char *user_path, int flags)
{
    char kernel_path[256];
    
    /* Validate path pointer (at least 1 byte) */
    if (!valid_user_pointer(user_path, 1)) {
        printk("[SYSCALL] open: Invalid path pointer\n");
        return -1;
    }
    
    /* Copy path string from userspace (with limit) */
    size_t len = 0;
    while (len < sizeof(kernel_path) - 1) {
        if (copy_from_user(&kernel_path[len], &user_path[len], 1) < 0) {
            return -1;
        }
        if (kernel_path[len] == '\0') {
            break;
        }
        len++;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';
    
    /* Call kernel VFS */
    return fs_open(kernel_path, flags);
}

/**
 * sys_write - Write to a file descriptor
 * @param fd File descriptor
 * @param user_buf User pointer to data buffer
 * @param count Number of bytes to write
 * @return Bytes written on success, -1 on error
 */
static int sys_write(int fd, const char *user_buf, size_t count)
{
    char kernel_buf[4096];
    
    /* Validate buffer */
    if (!valid_user_pointer(user_buf, count)) {
        printk("[SYSCALL] write: Invalid buffer (0x%08x, len=%u)\n",
               (uint32_t)user_buf, count);
        return -1;
    }
    
    
    /* Write in chunks to avoid large kernel buffer */
    size_t total_written = 0;
    while (total_written < count) {
        size_t chunk = count - total_written;
        if (chunk > sizeof(kernel_buf)) {
            chunk = sizeof(kernel_buf);
        }
        
        /* Copy chunk from userspace */
        if (copy_from_user(kernel_buf, user_buf + total_written, chunk) < 0) {
            return total_written > 0 ? (int)total_written : -1;
        }
        
        /* Write to VFS */
        int n = fs_write(fd, kernel_buf, chunk);
        if (n < 0) {
            return total_written > 0 ? (int)total_written : -1;
        }
        
        total_written += n;
        
        /* Short write - stop */
        if (n < (int)chunk) {
            break;
        }
    }
    
    return (int)total_written;
}

/**
 * sys_read - Read from a file descriptor
 * @param fd File descriptor
 * @param user_buf User pointer to buffer
 * @param count Number of bytes to read
 * @return Bytes read on success, 0 on EOF, -1 on error
 */
static int sys_read(int fd, char *user_buf, size_t count)
{
    char kernel_buf[4096];
    
    /* Validate buffer */
    if (!valid_user_pointer(user_buf, count)) {
        printk("[SYSCALL] read: Invalid buffer (0x%08x, len=%u)\n",
               (uint32_t)user_buf, count);
        return -1;
    }
    
    /* Read in chunks */
    size_t total_read = 0;
    while (total_read < count) {
        size_t chunk = count - total_read;
        if (chunk > sizeof(kernel_buf)) {
            chunk = sizeof(kernel_buf);
        }
        
        /* Read from VFS */
        int n = fs_read(fd, kernel_buf, chunk);
        if (n < 0) {
            return total_read > 0 ? (int)total_read : -1;
        }
        if (n == 0) {
            break;  /* EOF */
        }
        
        /* Copy to userspace */
        if (copy_to_user(user_buf + total_read, kernel_buf, n) < 0) {
            return total_read > 0 ? (int)total_read : -1;
        }
        
        total_read += n;
        
        /* Short read - stop */
        if (n < (int)chunk) {
            break;
        }
    }
    
    return (int)total_read;
}

/**
 * sys_close - Close a file descriptor
 * @param fd File descriptor
 * @return 0 on success, -1 on error
 */
static int sys_close(int fd)
{
    return fs_close(fd);
}

/**
 * sys_ioctl - I/O control operations on devices
 * @param fd File descriptor
 * @param cmd Control command
 * @param arg Command argument (optional, device-specific)
 * @return 0 on success, device-specific value, or -1 on error
 */
static int sys_ioctl(int fd, uint32_t cmd, uint32_t arg)
{
    return fs_ioctl(fd, cmd, arg);
}

/**
 * sys_dup - Duplicate a file descriptor
 * @param oldfd File descriptor to duplicate
 * @return New file descriptor on success, -1 on error
 * 
 * Creates a copy of oldfd using the lowest-numbered available fd.
 * The new fd shares the same open file description (offset, flags).
 */
static int sys_dup(int oldfd)
{
    task_struct_t *task = current;
    
    /* Find the old fd_entry */
    fd_entry_t *old_fde = NULL;
    list_head_t *pos;
    list_for_each(pos, &task->files) {
        fd_entry_t *fde = list_entry(pos, fd_entry_t, node);
        if (fde->fd == oldfd) {
            old_fde = fde;
            break;
        }
    }
    
    if (!old_fde || !old_fde->file) {
        printk("[SYSCALL] dup: invalid fd %d\n", oldfd);
        return -1;
    }
    
    /* Allocate new fd_entry */
    fd_entry_t *new_fde = (fd_entry_t *)kalloc(sizeof(fd_entry_t));
    if (!new_fde) {
        printk("[SYSCALL] dup: OOM\n");
        return -1;
    }
    
    /* New fd shares the same open_file */
    new_fde->fd = task->next_fd++;
    new_fde->file = old_fde->file;
    new_fde->file->refcount++;  /* Increment reference count */
    
    INIT_LIST_HEAD(&new_fde->node);
    list_add_tail(&new_fde->node, &task->files);
    
    return new_fde->fd;
}

/**
 * sys_dup2 - Duplicate a file descriptor to a specific fd number
 * @param oldfd File descriptor to duplicate
 * @param newfd Desired file descriptor number
 * @return newfd on success, -1 on error
 * 
 * If newfd is already open, it is closed first.
 * If oldfd == newfd, returns newfd without closing it.
 */
static int sys_dup2(int oldfd, int newfd)
{
    task_struct_t *task = current;
    
    /* Validate newfd range */
    if (newfd < 0 || newfd >= 1024) {
        printk("[SYSCALL] dup2: invalid newfd %d\n", newfd);
        return -1;
    }
    
    /* If oldfd == newfd, just validate and return */
    if (oldfd == newfd) {
        /* Check if oldfd is valid */
        list_head_t *pos;
        list_for_each(pos, &task->files) {
            fd_entry_t *fde = list_entry(pos, fd_entry_t, node);
            if (fde->fd == oldfd) {
                return newfd;  /* Valid, return as-is */
            }
        }
        printk("[SYSCALL] dup2: invalid oldfd %d\n", oldfd);
        return -1;
    }
    
    /* Find the old fd_entry */
    fd_entry_t *old_fde = NULL;
    list_head_t *pos;
    list_for_each(pos, &task->files) {
        fd_entry_t *fde = list_entry(pos, fd_entry_t, node);
        if (fde->fd == oldfd) {
            old_fde = fde;
            break;
        }
    }
    
    if (!old_fde || !old_fde->file) {
        printk("[SYSCALL] dup2: invalid oldfd %d\n", oldfd);
        return -1;
    }
    
    /* Close newfd if it's already open */
    fd_entry_t *new_fde = NULL;
    list_head_t *tmp;
    list_for_each_safe(pos, tmp, &task->files) {
        fd_entry_t *fde = list_entry(pos, fd_entry_t, node);
        if (fde->fd == newfd) {
            new_fde = fde;
            break;
        }
    }
    
    if (new_fde) {
        /* Close the existing newfd - just call fs_close which handles refcounting */
        fs_close(newfd);
        new_fde = NULL;  /* fs_close already freed it */
    }
    
    /* Create new fd_entry for newfd */
    new_fde = (fd_entry_t *)kalloc(sizeof(fd_entry_t));
    if (!new_fde) {
        printk("[SYSCALL] dup2: OOM\n");
        return -1;
    }
    
    /* New fd shares the same open_file */
    new_fde->fd = newfd;
    new_fde->file = old_fde->file;
    new_fde->file->refcount++;  /* Increment reference count */
    
    INIT_LIST_HEAD(&new_fde->node);
    list_add_tail(&new_fde->node, &task->files);
    
    /* Update next_fd if necessary */
    if (newfd >= task->next_fd) {
        task->next_fd = newfd + 1;
    }
    
    return newfd;
}

/**
 * sys_getpid - Get current process ID
 * @return Current process PID
 */
static int sys_getpid(void)
{
    return current->pid;
}

/**
 * sys_getppid - Get parent process ID
 * @return Parent process PID, or 0 if no parent
 */
static int sys_getppid(void)
{
    return current->ppid;
}

/**
 * sys_lseek - Reposition file offset
 * @param fd File descriptor
 * @param offset Offset value
 * @param whence Position reference (SEEK_SET, SEEK_CUR, SEEK_END)
 * @return New file offset on success, -1 on error
 */
static int sys_lseek(int fd, int32_t offset, int whence)
{
    return fs_seek(fd, offset, whence);
}

/**
 * sys_getcwd - Get current working directory
 * @param user_buf User buffer to store path
 * @param size Buffer size
 * @return Pointer to user_buf on success, -1 on error
 */
static int sys_getcwd(char *user_buf, size_t size)
{
    /* Validate user buffer */
    if (!valid_user_pointer(user_buf, size)) {
        printk("[SYSCALL] getcwd: Invalid buffer pointer\n");
        return -1;
    }
    
    /* Get current working directory from kernel */
    char *cwd = fs_getcwd(user_buf, size);
    if (!cwd) {
        printk("[SYSCALL] getcwd: Failed to get cwd\n");
        return -1;
    }
    
    /* fs_getcwd already wrote to user_buf, so we just return it */
    return (int)user_buf;
}

/**
 * sys_stat - Get file status by path
 * @param user_path User pointer to path string
 * @param user_stat User pointer to stat structure
 * @return 0 on success, -1 on error
 */
static int sys_stat(const char *user_path, stat_t *user_stat)
{
    char kernel_path[256];
    stat_t kernel_stat;
    
    /* Validate pointers */
    if (!valid_user_pointer(user_path, 1)) {
        printk("[SYSCALL] stat: Invalid path pointer\n");
        return -1;
    }
    
    if (!valid_user_pointer(user_stat, sizeof(stat_t))) {
        printk("[SYSCALL] stat: Invalid stat pointer\n");
        return -1;
    }
    
    /* Copy path string from userspace */
    size_t len = 0;
    while (len < sizeof(kernel_path) - 1) {
        if (copy_from_user(&kernel_path[len], &user_path[len], 1) < 0) {
            return -1;
        }
        if (kernel_path[len] == '\0') {
            break;
        }
        len++;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';
    
    /* Call VFS stat */
    if (fs_stat(kernel_path, &kernel_stat) < 0) {
        return -1;
    }
    
    /* Copy result to userspace */
    if (copy_to_user(user_stat, &kernel_stat, sizeof(stat_t)) < 0) {
        return -1;
    }
    
    return 0;
}

/**
 * sys_fstat - Get file status by file descriptor
 * @param fd File descriptor
 * @param user_stat User pointer to stat structure
 * @return 0 on success, -1 on error
 */
static int sys_fstat(int fd, stat_t *user_stat)
{
    /* TODO: Implement fstat via VFS
     * For now, return error as VFS doesn't have fstat yet */
    (void)fd;
    (void)user_stat;
    printk("[SYSCALL] fstat: Not yet implemented\n");
    return -1;
}

/**
 * sys_brk - Set program break (heap end)
 * @param addr New heap end address (0 = query current)
 * @return New heap end on success, -1 on error
 */
static int sys_brk(uint32_t addr)
{
    task_struct_t *task = current;
    
    /* Query current brk */
    if (addr == 0) {
        return (int)task->mm.brk_end;
    }
    
    /* Validate address is in heap region */
    if (addr < task->mm.brk_start) {
        printk("[SYSCALL] brk: addr 0x%08x < brk_start 0x%08x\n", addr, task->mm.brk_start);
        return -1;
    }
    
    /* Don't allow heap to grow into mmap region */
    if (addr >= task->mm.mmap_base) {
        printk("[SYSCALL] brk: addr 0x%08x >= mmap_base 0x%08x\n", addr, task->mm.mmap_base);
        return -1;
    }
    
    /* Find or create heap VMA */
    vma_t *heap_vma = NULL;
    list_head_t *pos;
    list_for_each(pos, &task->mm.vma_list) {
        vma_t *vma = list_entry(pos, vma_t, list);
        if (vma->vm_type == VMA_HEAP) {
            heap_vma = vma;
            break;
        }
    }
    
    /* Create heap VMA if it doesn't exist */
    if (!heap_vma) {
        heap_vma = vma_create(task->mm.brk_start, addr, 
                              VM_READ | VM_WRITE, VMA_HEAP);
        if (!heap_vma) {
            printk("[SYSCALL] brk: Failed to create heap VMA\n");
            return -1;
        }
        vma_insert(task, heap_vma);
    } else {
        /* Expand existing heap VMA */
        if (vma_expand(heap_vma, addr) < 0) {
            printk("[SYSCALL] brk: Failed to expand heap VMA\n");
            return -1;
        }
    }
    
    /* Update brk_end */
    task->mm.brk_end = addr;
    
    /* Pages will be allocated on-demand via page fault handler */
    
    return (int)addr;
}

/**
 * sys_mmap - Map anonymous memory
 * @param addr Hint address (0 = kernel chooses)
 * @param length Size of mapping
 * @param prot Protection flags (PROT_READ|PROT_WRITE|PROT_EXEC)
 * @param flags Mapping flags (MAP_PRIVATE|MAP_ANONYMOUS)
 * @param fd File descriptor (must be -1 for anonymous)
 * @param offset File offset (must be 0 for anonymous)
 * @return Mapped address on success, -1 on error
 */
static int sys_mmap(uint32_t addr, size_t length, int prot, int flags,
                    int fd, uint32_t offset)
{
    task_struct_t *task = current;
    
    /* Only support anonymous mapping for now */
    if (fd != -1 || offset != 0) {
        printk("[SYSCALL] mmap: File-backed mapping not yet supported\n");
        return -1;
    }
    
    if (!(flags & 0x20)) {  /* MAP_ANONYMOUS */
        printk("[SYSCALL] mmap: Non-anonymous mapping not supported\n");
        return -1;
    }
    
    /* Validate length */
    if (length == 0) {
        printk("[SYSCALL] mmap: Invalid length 0\n");
        return -1;
    }
    
    /* Find free region */
    uint32_t map_addr = vma_find_free_region(task, length, addr);
    if (map_addr == 0) {
        printk("[SYSCALL] mmap: No free region found for length %u\n", length);
        return -1;
    }
    
    /* Convert prot flags to VMA flags */
    uint32_t vm_flags = 0;
    if (prot & 0x1) vm_flags |= VM_READ;   /* PROT_READ */
    if (prot & 0x2) vm_flags |= VM_WRITE;  /* PROT_WRITE */
    if (prot & 0x4) vm_flags |= VM_EXEC;   /* PROT_EXEC */
    
    /* Create VMA for mmap region */
    vma_t *vma = vma_create(map_addr, map_addr + length, vm_flags, VMA_MMAP);
    if (!vma) {
        printk("[SYSCALL] mmap: Failed to create VMA\n");
        return -1;
    }
    
    vma_insert(task, vma);
    
    /* Pages will be allocated on-demand via page fault handler */
    
    return (int)map_addr;
}

/**
 * sys_munmap - Unmap memory region
 * @param addr Start address
 * @param length Size of region
 * @return 0 on success, -1 on error
 */
static int sys_munmap(uint32_t addr, size_t length)
{
    task_struct_t *task = current;
    
    /* Page-align address and length */
    uint32_t start = addr & ~0xFFF;
    uint32_t end = (addr + length + 0xFFF) & ~0xFFF;
    
    /* Find and remove overlapping VMAs */
    list_head_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &task->mm.vma_list) {
        vma_t *vma = list_entry(pos, vma_t, list);
        
        /* Check for overlap */
        if (start < vma->vm_end && end > vma->vm_start) {
            /* For simplicity, only handle exact match for now */
            if (start == vma->vm_start && end == vma->vm_end) {
                /* Unmap pages in this region */
                unmap_range(vma->vm_start, vma->vm_end);
                
                /* Remove and destroy VMA */
                vma_remove(task, vma);
                vma_destroy(vma);
            } else {
                printk("[SYSCALL] munmap: Partial unmap not yet supported\n");
                return -1;
            }
        }
    }
    
    return 0;
}

/* ============================================================================
 * System Call Dispatcher
 * ============================================================================ */

/**
 * Main syscall dispatcher - called from isr128 (int 0x80)
 */
int syscall_handler(registers_t *regs, int num, uint32_t arg1, uint32_t arg2,
                    uint32_t arg3, uint32_t arg4, uint32_t arg5)
{
    (void)arg4;  /* Unused */
    (void)arg5;  /* Unused */

    // printk("%x %x %x %x %x %x\n", num, arg1, arg2, arg3, arg4, arg5);
    
    switch (num) {
        case SYS_EXIT:
            sys_exit((int)arg1);
            /* Never returns */
            return 0;
        
        case SYS_FORK:
            return sys_fork(regs);
        
        case SYS_READ:
            return sys_read((int)arg1, (char *)arg2, (size_t)arg3);
        
        case SYS_WRITE:
            return sys_write((int)arg1, (const char *)arg2, (size_t)arg3);
        
        case SYS_OPEN:
            return sys_open((const char *)arg1, (int)arg2);
        
        case SYS_CLOSE:
            return sys_close((int)arg1);
        
        case SYS_LSEEK:
            return sys_lseek((int)arg1, (int32_t)arg2, (int)arg3);
        
        case SYS_GETPID:
            return sys_getpid();
        
        case SYS_DUP:
            return sys_dup((int)arg1);
        
        case SYS_BRK:
            return sys_brk(arg1);
        
        case SYS_IOCTL:
            return sys_ioctl((int)arg1, arg2, arg3);
        
        case SYS_DUP2:
            return sys_dup2((int)arg1, (int)arg2);
        
        case SYS_GETPPID:
            return sys_getppid();
        
        case SYS_WAIT4:
            return sys_wait((int *)arg1);
        
        case SYS_EXECVE:
            return sys_exec((const char *)arg1);
        
        case SYS_MMAP:
            return sys_mmap(arg1, (size_t)arg2, (int)arg3, 
                           (int)arg4, (int)arg5, 0);
        
        case SYS_MUNMAP:
            return sys_munmap(arg1, (size_t)arg2);
        
        case SYS_STAT:
            return sys_stat((const char *)arg1, (stat_t *)arg2);
        
        case SYS_FSTAT:
            return sys_fstat((int)arg1, (stat_t *)arg2);
        
        case SYS_GETCWD:
            return sys_getcwd((char *)arg1, (size_t)arg2);
        
        default:
            printk("[SYSCALL] Unknown syscall number: %d\n", num);
            return -1;
    }
}
