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
#include "driver/char/pit.h"
#include "driver/char/rtc.h"
#include "lib/printk.h"
#include "errno.h"
#include "lib/string.h"

/* Kernel virtual memory starts at 3GB */
#define KERNEL_VMA 0xC0000000

/* ============================================================================
 * User Pointer Validation
 * ============================================================================ */

/**
 * check_user_range - Verify a user memory range is mapped in the current
 * task's page tables.
 *
 * Walks current->mm.page_tables[] (the same table the running user CR3 is
 * built from).  Every page in [addr, addr+len) must be present; when
 * need_write is set, pages must also be writable.  Returns 1 if valid,
 * 0 otherwise.
 */
static int check_user_range(const void *ptr, size_t len, int need_write)
{
    uint32_t addr = (uint32_t)ptr;
    uint32_t end  = addr + len;

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

    if (len == 0) {
        return 1;
    }

    task_struct_t *task = current;
    if (!task) {
        return 0;
    }

    for (uint32_t a = addr; a < end; a += 0x1000) {
        uint32_t pdi = a >> 22;
        if (pdi >= 768) {
            return 0;   /* above 3GB is kernel space */
        }
        uint32_t *pt = task->mm.page_tables[pdi];
        if (!pt) {
            return 0;   /* no page table for this 4MB region */
        }
        uint32_t pte = pt[(a >> 12) & 0x3FF];
        if (!(pte & 0x1)) {
            return 0;   /* not present */
        }
        if (need_write && !(pte & 0x2)) {
            return 0;   /* read-only */
        }
    }

    return 1;
}

/**
 * Validate a user pointer is within user address space
 * User space: 0x00000000 - 0xBFFFFFFF
 * Kernel space: 0xC0000000 - 0xFFFFFFFF
 */
int valid_user_pointer(const void *ptr, size_t len)
{
    return check_user_range(ptr, len, 0);
}

/**
 * Copy data from userspace to kernel space safely
 */
int copy_from_user(void *kernel_dst, const void *user_src, size_t n)
{
    /* Validate user pointer */
    if (!valid_user_pointer(user_src, n)) {
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
    /* Validate user pointer AND writability (page-table walk) */
    if (!check_user_range(user_dst, n, 1)) {
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
        return -EFAULT;
    }
    
    /* Copy path string from userspace (with limit) */
    size_t len = 0;
    while (len < sizeof(kernel_path) - 1) {
        if (copy_from_user(&kernel_path[len], &user_path[len], 1) < 0) {
            return -EFAULT;
        }
        if (kernel_path[len] == '\0') {
            break;
        }
        len++;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';
    
    /* Permission check: existing files require R_OK / W_OK per the open
     * mode.  If the file does not exist yet (O_CREAT), fs_access_perm
     * returns -ENOENT which we allow so the create can proceed. */
    int need = (flags & (O_WRONLY | O_RDWR)) ? 2 : 4;   /* W_OK / R_OK */
    int r = fs_access_perm(kernel_path, need);
    if (r < 0 && r != -ENOENT)
        return r;
    
    /* Call kernel VFS */
    int fd = fs_open(kernel_path, flags);
    if (fd < 0)
        return -ENOENT;
    return fd;
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
        return -EFAULT;
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
            return total_written > 0 ? (int)total_written : -EFAULT;
        }
        
        /* Write to VFS */
        int n = fs_write(fd, kernel_buf, chunk);
        if (n < 0) {
            return total_written > 0 ? (int)total_written : -EIO;
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
        return -EFAULT;
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
            return total_read > 0 ? (int)total_read : n;
        }
        if (n == 0) {
            break;  /* EOF */
        }
        
        /* Copy to userspace */
        if (copy_to_user(user_buf + total_read, kernel_buf, n) < 0) {
            return total_read > 0 ? (int)total_read : -EFAULT;
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
    if (fs_close(fd) < 0)
        return -EBADF;
    return 0;
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
    int r = fs_ioctl(fd, cmd, arg);
    if (r < 0)
        return -ENOTTY;
    return r;
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
        return -EBADF;
    }
    
    /* Allocate new fd_entry */
    fd_entry_t *new_fde = (fd_entry_t *)kalloc(sizeof(fd_entry_t));
    if (!new_fde) {
        return -ENOMEM;
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
        return -EBADF;
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
        return -EBADF;
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
        return -EBADF;
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
        return -ENOMEM;
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
    int r = fs_seek(fd, offset, whence);
    if (r < 0)
        return -EINVAL;
    return r;
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
        return -EFAULT;
    }
    
    /* Get current working directory from kernel */
    char *cwd = fs_getcwd(user_buf, size);
    if (!cwd) {
        return -ERANGE;
    }
    
    /* fs_getcwd already wrote to user_buf; 0 signals success */
    return 0;
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
        return -EFAULT;
    }
    
    if (!valid_user_pointer(user_stat, sizeof(stat_t))) {
        return -EFAULT;
    }
    
    /* Copy path string from userspace */
    size_t len = 0;
    while (len < sizeof(kernel_path) - 1) {
        if (copy_from_user(&kernel_path[len], &user_path[len], 1) < 0) {
            return -EFAULT;
        }
        if (kernel_path[len] == '\0') {
            break;
        }
        len++;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';
    
    /* Call VFS stat */
    if (fs_stat(kernel_path, &kernel_stat) < 0) {
        return -ENOENT;
    }
    
    /* Copy result to userspace */
    if (copy_to_user(user_stat, &kernel_stat, sizeof(stat_t)) < 0) {
        return -EFAULT;
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
    if (!user_stat || !valid_user_pointer(user_stat, sizeof(stat_t))) {
        return -EFAULT;
    }

    stat_t st;
    if (fs_fstat(fd, &st) < 0)
        return -EBADF;

    if (copy_to_user(user_stat, &st, sizeof(st)) < 0)
        return -EFAULT;
    return 0;
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
        return -ENOMEM;
    }
    
    /* Don't allow heap to grow into mmap region */
    if (addr >= task->mm.mmap_base) {
        return -ENOMEM;
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
            return -ENOMEM;
        }
        vma_insert(task, heap_vma);
    } else {
        /* Expand existing heap VMA */
        if (vma_expand(heap_vma, addr) < 0) {
            return -ENOMEM;
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
        return -EINVAL;
    }
    
    if (!(flags & 0x20)) {  /* MAP_ANONYMOUS */
        return -EINVAL;
    }
    
    /* Validate length */
    if (length == 0) {
        return -EINVAL;
    }
    
    /* Find free region */
    uint32_t map_addr = vma_find_free_region(task, length, addr);
    if (map_addr == 0) {
        return -ENOMEM;
    }
    
    /* Convert prot flags to VMA flags */
    uint32_t vm_flags = 0;
    if (prot & 0x1) vm_flags |= VM_READ;   /* PROT_READ */
    if (prot & 0x2) vm_flags |= VM_WRITE;  /* PROT_WRITE */
    if (prot & 0x4) vm_flags |= VM_EXEC;   /* PROT_EXEC */
    
    /* Create VMA for mmap region */
    vma_t *vma = vma_create(map_addr, map_addr + length, vm_flags, VMA_MMAP);
    if (!vma) {
        return -ENOMEM;
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
                return -EINVAL;
            }
        }
    }
    
    return 0;
}

/* ============================================================================
 * Pipes
 * ============================================================================ */

/**
 * sys_gettimeofday - Get wall-clock time
 * @param user_tv User pointer to struct timeval (seconds + microseconds)
 * @param user_tz Timezone pointer; ignored (must be NULL)
 * @return 0 on success, -1 on error
 *
 * Wall clock = RTC time captured at boot + PIT uptime (100 Hz ticks).
 */
static int sys_gettimeofday(struct timeval *user_tv, void *user_tz)
{
    (void)user_tz;

    if (!valid_user_pointer(user_tv, sizeof(struct timeval)))
        return -EFAULT;

    struct timeval tv;
    uint32_t ticks = pit_get_ticks();

    tv.tv_sec  = (int32_t)rtc_get_boot_unix_time() + (int32_t)(ticks / 100);
    tv.tv_usec = (int32_t)((ticks % 100) * 10000);   /* 10 ms per tick */

    if (copy_to_user(user_tv, &tv, sizeof(struct timeval)) < 0)
        return -EFAULT;

    return 0;
}

/**
 * sys_pipe - Create an anonymous pipe
 * @param user_fds User array of 2 ints; filled with [read_end, write_end]
 * @return 0 on success, -1 on error
 */
static int sys_pipe(int *user_fds)
{
    if (!valid_user_pointer(user_fds, 2 * sizeof(int))) {
        return -EFAULT;
    }

    int fds[2];
    if (fs_pipe(fds) < 0)
        return -ENFILE;

    if (copy_to_user(user_fds, fds, 2 * sizeof(int)) < 0)
        return -EFAULT;

    return 0;
}

/* ============================================================================
 * Path-based filesystem operations
 * ============================================================================ */

/* Copy a NUL-terminated path from userspace into kernel buffer. */
static int copy_path_from_user(const char *user_path, char *kernel_path,
                               size_t size)
{
    if (!valid_user_pointer(user_path, 1))
        return -1;

    size_t len = 0;
    while (len < size - 1) {
        if (copy_from_user(&kernel_path[len], &user_path[len], 1) < 0)
            return -1;
        if (kernel_path[len] == '\0')
            return 0;
        len++;
    }
    kernel_path[size - 1] = '\0';
    return 0;
}

static int sys_mkdir(const char *user_path, uint32_t mode)
{
    char kernel_path[MAX_PATH_LEN];
    if (copy_path_from_user(user_path, kernel_path, sizeof(kernel_path)) < 0)
        return -EFAULT;
    if (mode == 0)
        mode = 0777;   /* default permissions when the caller passes 0 */
    if (fs_mkdir(kernel_path, mode) < 0)
        return -EEXIST;
    return 0;
}

static int sys_rmdir(const char *user_path)
{
    char kernel_path[MAX_PATH_LEN];
    if (copy_path_from_user(user_path, kernel_path, sizeof(kernel_path)) < 0)
        return -EFAULT;
    int r = fs_access_perm(kernel_path, 2);   /* W_OK on the directory */
    if (r < 0)
        return r;
    if (fs_rmdir(kernel_path) < 0)
        return -ENOENT;
    return 0;
}

static int sys_unlink(const char *user_path)
{
    char kernel_path[MAX_PATH_LEN];
    if (copy_path_from_user(user_path, kernel_path, sizeof(kernel_path)) < 0)
        return -EFAULT;
    int r = fs_access_perm(kernel_path, 2);   /* W_OK on the target */
    if (r < 0)
        return r;
    r = fs_unlink(kernel_path);
    if (r < 0)
        return -ENOENT;
    return 0;
}

static int sys_rename(const char *user_old, const char *user_new)
{
    char kernel_old[MAX_PATH_LEN], kernel_new[MAX_PATH_LEN];
    if (copy_path_from_user(user_old, kernel_old, sizeof(kernel_old)) < 0)
        return -EFAULT;
    if (copy_path_from_user(user_new, kernel_new, sizeof(kernel_new)) < 0)
        return -EFAULT;
    int r = fs_access_perm(kernel_old, 2);   /* W_OK on the source */
    if (r < 0)
        return r;
    r = fs_access_perm(kernel_new, 2);       /* W_OK on the destination */
    if (r < 0 && r != -ENOENT)
        return r;
    if (fs_rename(kernel_old, kernel_new) < 0)
        return -ENOENT;
    return 0;
}

static int sys_truncate(const char *user_path, uint32_t length)
{
    /* fs_truncate() works on an open fd, so open + truncate + close. */
    char kernel_path[MAX_PATH_LEN];
    if (copy_path_from_user(user_path, kernel_path, sizeof(kernel_path)) < 0)
        return -EFAULT;

    int r = fs_access_perm(kernel_path, 2);   /* W_OK */
    if (r < 0)
        return r;

    int fd = fs_open(kernel_path, O_WRONLY);
    if (fd < 0)
        return -ENOENT;
    int ret = fs_truncate(fd, length);
    fs_close(fd);
    if (ret < 0)
        return -EINVAL;
    return 0;
}

/* ============================================================================
 * Access / uname
 * ============================================================================ */

/**
 * sys_access - Check whether the calling process can access a file.
 * @param user_path User pointer to path
 * @param mode      0 = existence (F_OK); X_OK/W_OK/R_OK are accepted but the
 *                  kernel has no permission model yet, so they behave like F_OK
 * @return 0 on success (file exists), -errno otherwise
 */
static int sys_access(const char *user_path, int mode)
{
    char kernel_path[MAX_PATH_LEN];

    if (copy_path_from_user(user_path, kernel_path, sizeof(kernel_path)) < 0)
        return -EFAULT;

    /* F_OK (0) checks existence; R_OK/W_OK/X_OK check permissions. */
    return fs_access_perm(kernel_path, mode & 7);
}

/* Kernel utsname (must match the user struct in crt/include/sys/utsname.h) */
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

/**
 * sys_uname - Fill in system identification information.
 * @param user_buf User pointer to a struct utsname
 * @return 0 on success, -errno otherwise
 */
static int sys_uname(struct utsname *user_buf)
{
    if (!valid_user_pointer(user_buf, sizeof(struct utsname)))
        return -EFAULT;

    struct utsname u;
    strcpy(u.sysname,  "Scepter");
    strcpy(u.nodename, "scepter");
    strcpy(u.release,  "0.1");
    strcpy(u.version,  "Scepter OS 0.1");
    strcpy(u.machine,  "i386");

    if (copy_to_user(user_buf, &u, sizeof(u)) < 0)
        return -EFAULT;
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
        
        case SYS_NANOSLEEP:
            return sys_nanosleep((timespec_t *)arg1, (timespec_t *)arg2);
        
        case SYS_WAIT4:
            return sys_wait4((int)arg1, (int *)arg2, (int)arg3, (void *)arg4);
        
        case SYS_PIPE:
            return sys_pipe((int *)arg1);
        
        case SYS_MKDIR:
            return sys_mkdir((const char *)arg1, (uint32_t)arg2);
        
        case SYS_RMDIR:
            return sys_rmdir((const char *)arg1);
        
        case SYS_UNLINK:
            return sys_unlink((const char *)arg1);
        
        case SYS_RENAME:
            return sys_rename((const char *)arg1, (const char *)arg2);
        
        case SYS_TRUNCATE:
            return sys_truncate((const char *)arg1, (uint32_t)arg2);
        
        case SYS_GETTIMEOFDAY:
            return sys_gettimeofday((struct timeval *)arg1, (void *)arg2);
        
        case SYS_EXEC:
            return sys_exec((const char *)arg1);
        
        case SYS_EXECV:
            return sys_execv((const char *)arg1, (char **)arg2);
        
        case SYS_EXECVE:
            return sys_execve((const char *)arg1, (char **)arg2, (char **)arg3);
        
        case SYS_CHDIR:
            return sys_chdir((const char *)arg1);
        
        case SYS_GETDENTS:
            return sys_getdents((int)arg1, (dirent_t *)arg2, (unsigned int)arg3);
        
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
        
        case SYS_SIGNAL:
            return sys_signal((int)arg1, (uint32_t)arg2);
        
        case SYS_KILL:
            return sys_kill((int)arg1, (int)arg2);
        
        case SYS_SIGRETURN:
            return sys_sigreturn(regs);
        
        case SYS_NICE:
            return sys_nice((int)arg1);

        case SYS_ACCESS:
            return sys_access((const char *)arg1, (int)arg2);

        case SYS_UNAME:
            return sys_uname((struct utsname *)arg1);
        
        case SYS_SETUID:
            return sys_setuid(arg1);

        case SYS_GETUID:
            return sys_getuid();

        case SYS_SETGID:
            return sys_setgid(arg1);

        case SYS_GETGID:
            return sys_getgid();

        case SYS_GETEUID:
            return sys_geteuid();

        case SYS_GETEGID:
            return sys_getegid();

        case SYS_SETPGID:
            return sys_setpgid((int)arg1, (int)arg2);

        case SYS_GETPGRP:
            return sys_getpgrp();

        case SYS_SETSID:
            return sys_setsid();

        case SYS_GETPGID:
            return sys_getpgid((int)arg1);

        case SYS_GETSID:
            return sys_getsid((int)arg1);

        case SYS_SIGACTION:
            return sys_sigaction((int)arg1, (sigaction_t *)arg2,
                                 (sigaction_t *)arg3);

        case SYS_SIGPROCMASK:
            return sys_sigprocmask((int)arg1, (sigset_t *)arg2,
                                   (sigset_t *)arg3);

        case SYS_SIGPENDING:
            return sys_sigpending((sigset_t *)arg1);

        case SYS_SIGSUSPEND:
            return sys_sigsuspend((sigset_t *)arg1);

        default:
            printk("[SYSCALL] Unknown syscall number: %d\n", num);
            return -1;
    }
}
