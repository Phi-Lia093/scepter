/* ============================================================================
 * System Call Implementation
 * ============================================================================ */

#include "kernel/syscall.h"
#include "kernel/syscall_new.h"
#include "kernel/sched.h"
#include "kernel/process.h"
#include "mm/vma.h"
#include "mm/pgtable.h"
#include "mm/slab.h"
#include "fs/fs.h"
#include "fs/pipe.h"
#include "driver/char/pit.h"
#include "driver/char/rtc.h"
#include "lib/printk.h"
#include "errno.h"
#include "lib/string.h"

/* select()/poll() wait queue (defined in fs/vfs.c) */
extern wait_queue_head_t select_wq;

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
static int sys_open(const char *user_path, int flags, uint32_t mode)
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
    
    /* Apply the umask to the creation mode (Linux masks in the kernel). */
    mode &= ~current->umask;

    /* Call kernel VFS */
    int fd = fs_open(kernel_path, flags, mode);
    if (fd < 0) {
        /* Propagate a specific errno (e.g. FIFO ENXIO); default to ENOENT. */
        if (fd < -1 && fd >= -4095)
            return fd;
        return -ENOENT;
    }
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
    int r = fs_dup_min(oldfd, 0, 0);
    if (r < 0)
        return -EBADF;
    return r;
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
    int r = fs_dup2_fd(oldfd, newfd, 0);
    if (r < 0)
        return -EBADF;
    return r;
}

static int sys_dup3(int oldfd, int newfd, int flags)
{
    int cloexec = (flags & O_CLOEXEC) ? 1 : 0;
    int r = fs_dup2_fd(oldfd, newfd, cloexec);
    if (r < 0)
        return -EBADF;
    return r;
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
    
    /* Enforce RLIMIT_AS (address space limit). */
    if (task->rlimit_cur[RLIMIT_AS] != RLIM_INFINITY &&
        addr - USER_TEXT_START > task->rlimit_cur[RLIMIT_AS])
        return -ENOMEM;
    
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
/**
 * task_get_pte - Resolve a PTE in a specific task's page directory.
 * Unlike get_pte() (which walks boot_page_directory), this works for
 * user mappings while the task's own pgdir is active in CR3.
 */
static uint32_t *task_get_pte(task_struct_t *task, uint32_t virt_addr)
{
    uint32_t pde_idx = virt_addr >> 22;
    uint32_t pte_idx = (virt_addr >> 12) & 0x3FF;

    uint32_t pde = task->mm.pgdir[pde_idx];
    if (!(pde & 0x1))
        return NULL;
    uint32_t *pt = (uint32_t *)((pde & ~0xFFF) + KERNEL_VMA);
    return &pt[pte_idx];
}

/** Unmap a range of user pages in the current task's page directory. */
static void user_unmap_range(uint32_t virt_start, uint32_t virt_end)
{
    for (uint32_t a = virt_start & ~0xFFF; a < virt_end; a += 0x1000) {
        uint32_t *pte = task_get_pte(current, a);
        if (pte && (*pte & 0x1)) {
            *pte = 0;
            asm volatile("invlpg (%0)" :: "r"(a) : "memory");
        }
    }
}

/**
 * sys_mmap - Map memory.
 */
static int sys_mmap(uint32_t addr, size_t length, int prot, int flags,
                    int fd, uint32_t offset)
{
    task_struct_t *task = current;
    
    /* Validate length */
    if (length == 0) {
        return -EINVAL;
    }
    
    int is_anon = (flags & 0x20) != 0;   /* MAP_ANONYMOUS */
    if (!is_anon) {
        if (fd < 0 || !fs_fd_valid(fd))
            return -EBADF;
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
    if (flags & 0x1) vm_flags |= VM_SHARED; /* MAP_SHARED */
    
    /* Create VMA for mmap region */
    vma_t *vma = vma_create(map_addr, map_addr + length, vm_flags, VMA_MMAP);
    if (!vma) {
        return -ENOMEM;
    }
    
    /* File-backed mapping: remember the fd + offset.  The fd's refcount
     * keeps the open_file alive for the lifetime of the VMA. */
    vma->vm_fd       = is_anon ? -1 : fd;
    vma->vm_file_off = offset;
    vma->vm_shared   = (flags & 0x1) ? 1 : 0;
    
    vma_insert(task, vma);
    
    /* Pages will be allocated on-demand via page fault handler */
    
    return (int)map_addr;
}

/**
 * sys_mprotect - Change protection on a memory region.
 * @param addr Start address
 * @param length Size of region
 * @param prot PROT_NONE / PROT_READ / PROT_WRITE / PROT_EXEC
 * @return 0 on success
 */
static int sys_mprotect(uint32_t addr, size_t length, int prot)
{
    task_struct_t *task = current;

    uint32_t start = addr & ~0xFFF;
    uint32_t end   = (addr + length + 0xFFF) & ~0xFFF;
    if (end <= start)
        return -EINVAL;

    vma_t *vma = vma_find(task, start);
    if (!vma || start < vma->vm_start || end > vma->vm_end)
        return -ENOMEM;   /* region must be covered by a single VMA */

    uint32_t vm_flags = 0;
    if (prot & 0x1) vm_flags |= VM_READ;
    if (prot & 0x2) vm_flags |= VM_WRITE;
    if (prot & 0x4) vm_flags |= VM_EXEC;

    /* Preserve shared/growsdown attributes. */
    vma->vm_flags = (vma->vm_flags & (VM_SHARED | VM_GROWSDOWN)) | vm_flags;

    /* Update PTEs for already-mapped pages. */
    for (uint32_t a = start; a < end; a += 0x1000) {
        uint32_t *pte = task_get_pte(task, a);
        if (pte && (*pte & 0x1)) {
            if (vm_flags & VM_WRITE)
                *pte |= 0x2;       /* R/W */
            else
                *pte &= ~0x2;      /* read-only */
            asm volatile("invlpg (%0)" :: "r"(a) : "memory");
        }
    }

    return 0;
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
                /* MAP_SHARED file-backed: write dirty pages back to the
                 * file before unmapping. */
                if (vma->vm_fd >= 0 && vma->vm_shared) {
                    for (uint32_t a = vma->vm_start; a < vma->vm_end;
                         a += 0x1000) {
                        uint32_t *pte = task_get_pte(task, a);
                        if (pte && (*pte & 0x1)) {
                            uint32_t file_off =
                                vma->vm_file_off + (a - vma->vm_start);
                            char page[0x1000];
                            if (copy_from_user(page, (void *)a, 0x1000) == 0)
                                fs_pwrite(vma->vm_fd, page, 0x1000, file_off);
                        }
                    }
                }

                /* Unmap pages in this region (in the task's own pgdir) */
                user_unmap_range(vma->vm_start, vma->vm_end);
                
                /* vma_destroy unlinks from the list AND frees the VMA.
                 * (vma_remove must NOT be called first: its list_del
                 * nulls the links, and vma_destroy would then double-del.) */
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
int copy_path_from_user(const char *user_path, char *kernel_path,
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
    mode &= ~current->umask;
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

    int fd = fs_open(kernel_path, O_WRONLY, 0);
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

/* Kernel utsname type is declared in kernel/syscall.h; the mutable global
 * system identity lives here (sethostname/setdomainname update it). */
struct utsname sys_utsname = {
    .sysname  = "Scepter",
    .nodename = "scepter",
    .release  = "0.1",
    .version  = "Scepter OS 0.1",
    .machine  = "i386",
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

    if (copy_to_user(user_buf, &sys_utsname, sizeof(sys_utsname)) < 0)
        return -EFAULT;
    return 0;
}

/* ============================================================================
 * fcntl()
 * ============================================================================ */

#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4
#define F_GETLK  5
#define F_SETLK  6
#define F_SETLKW 7
#define F_SETOWN 8
#define F_GETOWN 9
#define FD_CLOEXEC 1

static int sys_fcntl(int fd, int cmd, uint32_t arg)
{
    task_struct_t *task = current;

    switch (cmd) {
        case F_DUPFD: {
            int r = fs_dup_min(fd, (int)arg, 0);
            if (r < 0)
                return -EBADF;
            return r;
        }
        case F_GETFD: {
            int r = fs_get_cloexec(fd);
            if (r < 0)
                return -EBADF;
            return r ? FD_CLOEXEC : 0;
        }
        case F_SETFD: {
            if (fs_set_cloexec(fd, (arg & FD_CLOEXEC) ? 1 : 0) < 0)
                return -EBADF;
            return 0;
        }
        case F_GETFL:
        case F_GETOWN: {
            open_file_t *file = NULL;
            list_head_t *pos;
            list_for_each(pos, &task->files) {
                fd_entry_t *e = list_entry(pos, fd_entry_t, node);
                if (e->fd == fd) { file = e->file; break; }
            }
            if (!file)
                return -EBADF;
            return (cmd == F_GETFL) ? file->flags : file->owner;
        }
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
            return fs_fcntl_lock(fd, cmd, (struct flock_k *)arg);

        case F_SETFL:
        case F_SETOWN: {
            open_file_t *file = NULL;
            list_head_t *pos;
            list_for_each(pos, &task->files) {
                fd_entry_t *e = list_entry(pos, fd_entry_t, node);
                if (e->fd == fd) { file = e->file; break; }
            }
            if (!file)
                return -EBADF;
            if (cmd == F_SETFL) {
                /* Only the settable flags change; access mode preserved. */
                file->flags = (file->flags & O_ACCMODE) | (arg & ~O_ACCMODE);
            } else {
                file->owner = (int)arg;
            }
            return 0;
        }
        default:
            return -EINVAL;
    }
}

/* ============================================================================
 * select() / poll()
 * ============================================================================ */

#define FD_SETSIZE 256
#define FD_SETSIZE_BYTES (FD_SETSIZE / 8)

struct pollfd_k {
    int      fd;
    short    events;
    short    revents;
};

static void fdset_zero(unsigned char *set)
{
    memset(set, 0, FD_SETSIZE_BYTES);
}

static int fdset_isset(const unsigned char *set, int fd)
{
    return (set[fd >> 3] >> (fd & 7)) & 1;
}

static void fdset_set(unsigned char *set, int fd)
{
    set[fd >> 3] |= (1 << (fd & 7));
}

/**
 * do_poll - Core poll loop shared by poll() and select().
 * @param fds       Array of (fd, events) pairs (kernel copies)
 * @param nfds      Number of pairs
 * @param timeout   Timeout in ms; -1 = block forever
 * @return Number of fds with revents != 0
 */
static int do_poll(struct pollfd_k *fds, int nfds, int timeout)
{
    uint32_t deadline = 0;
    if (timeout >= 0) {
        /* 100 Hz tick = 10 ms per tick */
        uint32_t ticks = (uint32_t)((timeout + 9) / 10);
        deadline = pit_get_ticks() + ticks;
    }

    for (;;) {
        int ready = 0;
        for (int i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            if (fds[i].fd < 0)
                continue;   /* ignored */
            int mask = fs_poll(fds[i].fd);
            if (mask & POLLNVAL) {
                fds[i].revents = POLLNVAL;
                ready++;
            } else {
                fds[i].revents = mask & (fds[i].events | POLLERR |
                                         POLLHUP | POLLNVAL);
                if (fds[i].revents)
                    ready++;
            }
        }
        if (ready > 0)
            return ready;

        if (timeout == 0)
            return 0;
        if (deadline && (int32_t)(pit_get_ticks() - deadline) >= 0)
            return 0;
        if (current->pending)
            return -EINTR;

        sleep_on(&select_wq);
    }
}

static int sys_poll(struct pollfd_k *user_fds, int nfds, int timeout)
{
    if (!user_fds || nfds < 0)
        return -EINVAL;
    if (nfds > 64)
        nfds = 64;

    struct pollfd_k fds[64];
    if (nfds > 0) {
        if (!valid_user_pointer(user_fds, nfds * sizeof(struct pollfd_k)))
            return -EFAULT;
        if (copy_from_user(fds, user_fds, nfds * sizeof(struct pollfd_k)) < 0)
            return -EFAULT;
    }

    int r = do_poll(fds, nfds, timeout);
    if (r < 0)
        return r;

    if (copy_to_user(user_fds, fds, nfds * sizeof(struct pollfd_k)) < 0)
        return -EFAULT;
    return r;
}

static int sys_select(int nfds, void *user_read, void *user_write,
                      void *user_except, void *user_timeout)
{
    unsigned char read_set[FD_SETSIZE_BYTES];
    unsigned char write_set[FD_SETSIZE_BYTES];
    unsigned char except_set[FD_SETSIZE_BYTES];
    unsigned char out_read[FD_SETSIZE_BYTES];
    unsigned char out_write[FD_SETSIZE_BYTES];
    unsigned char out_except[FD_SETSIZE_BYTES];

    if (nfds < 0 || nfds > FD_SETSIZE)
        return -EINVAL;

    fdset_zero(read_set);
    fdset_zero(write_set);
    fdset_zero(except_set);
    fdset_zero(out_read);
    fdset_zero(out_write);
    fdset_zero(out_except);

    if (user_read && !valid_user_pointer(user_read, FD_SETSIZE_BYTES))
        return -EFAULT;
    if (user_write && !valid_user_pointer(user_write, FD_SETSIZE_BYTES))
        return -EFAULT;
    if (user_except && !valid_user_pointer(user_except, FD_SETSIZE_BYTES))
        return -EFAULT;

    if (user_read)
        copy_from_user(read_set, user_read, FD_SETSIZE_BYTES);
    if (user_write)
        copy_from_user(write_set, user_write, FD_SETSIZE_BYTES);
    if (user_except)
        copy_from_user(except_set, user_except, FD_SETSIZE_BYTES);

    /* Build the poll array. */
    struct pollfd_k fds[FD_SETSIZE];
    int n = 0;
    for (int fd = 0; fd < nfds; fd++) {
        short ev = 0;
        if (user_read && fdset_isset(read_set, fd))
            ev |= POLLIN;
        if (user_write && fdset_isset(write_set, fd))
            ev |= POLLOUT;
        if (user_except && fdset_isset(except_set, fd))
            ev |= POLLPRI;
        if (ev) {
            fds[n].fd = fd;
            fds[n].events = ev;
            fds[n].revents = 0;
            n++;
        }
    }

    /* Timeout: NULL = forever, else timeval. */
    int timeout = -1;
    if (user_timeout) {
        if (!valid_user_pointer(user_timeout, sizeof(timeval_t)))
            return -EFAULT;
        timeval_t tv;
        if (copy_from_user(&tv, user_timeout, sizeof(tv)) < 0)
            return -EFAULT;
        timeout = (int)(tv.tv_sec * 1000) + (int)(tv.tv_usec / 1000);
        if (timeout < 0)
            timeout = 0;
    }

    int r = do_poll(fds, n, timeout);
    if (r < 0)
        return r;

    /* Map results back into the fd_sets. */
    for (int i = 0; i < n; i++) {
        if (fds[i].revents & (POLLIN | POLLPRI | POLLHUP | POLLERR))
            fdset_set(out_read, fds[i].fd);
        if (fds[i].revents & (POLLOUT | POLLERR))
            fdset_set(out_write, fds[i].fd);
        if (fds[i].revents & POLLPRI)
            fdset_set(out_except, fds[i].fd);
    }

    if (user_read && copy_to_user(user_read, out_read, FD_SETSIZE_BYTES) < 0)
        return -EFAULT;
    if (user_write && copy_to_user(user_write, out_write, FD_SETSIZE_BYTES) < 0)
        return -EFAULT;
    if (user_except && copy_to_user(user_except, out_except, FD_SETSIZE_BYTES) < 0)
        return -EFAULT;

    return r;
}

/* ============================================================================
 * Vector / positional I/O
 * ============================================================================ */

struct iovec_k {
    void    *iov_base;
    uint32_t iov_len;
};

static int sys_readv(int fd, struct iovec_k *user_vec, int count)
{
    if (count < 0)
        return -EINVAL;
    if (count > 16)
        count = 16;

    struct iovec_k vec[16];
    if (count > 0) {
        if (!valid_user_pointer(user_vec, count * sizeof(struct iovec_k)))
            return -EFAULT;
        if (copy_from_user(vec, user_vec, count * sizeof(struct iovec_k)) < 0)
            return -EFAULT;
    }

    int total = 0;
    for (int i = 0; i < count; i++) {
        int n = sys_read(fd, vec[i].iov_base, vec[i].iov_len);
        if (n < 0)
            return total > 0 ? total : n;
        total += n;
        if (n < (int)vec[i].iov_len)
            break;   /* EOF or short read */
    }
    return total;
}

static int sys_writev(int fd, struct iovec_k *user_vec, int count)
{
    if (count < 0)
        return -EINVAL;
    if (count > 16)
        count = 16;

    struct iovec_k vec[16];
    if (count > 0) {
        if (!valid_user_pointer(user_vec, count * sizeof(struct iovec_k)))
            return -EFAULT;
        if (copy_from_user(vec, user_vec, count * sizeof(struct iovec_k)) < 0)
            return -EFAULT;
    }

    int total = 0;
    for (int i = 0; i < count; i++) {
        int n = sys_write(fd, vec[i].iov_base, vec[i].iov_len);
        if (n < 0)
            return total > 0 ? total : n;
        total += n;
        if (n < (int)vec[i].iov_len)
            break;
    }
    return total;
}

static int sys_pread(int fd, void *buf, size_t count, uint32_t offset)
{
    if (!valid_user_pointer(buf, count))
        return -EFAULT;
    return fs_pread(fd, buf, count, offset);
}

static int sys_pwrite(int fd, const void *buf, size_t count, uint32_t offset)
{
    if (!valid_user_pointer(buf, count))
        return -EFAULT;
    return fs_pwrite(fd, buf, count, offset);
}

static int sys_ftruncate(int fd, uint32_t length)
{
    if (fs_truncate(fd, length) < 0)
        return -EINVAL;
    return 0;
}

static int sys_fsync(int fd)
{
    /* minix3 writes are synchronous; nothing to flush. */
    if (!fs_fd_valid(fd))
        return -EBADF;
    return 0;
}

static int sys_fdatasync(int fd)
{
    return sys_fsync(fd);
}

/* ============================================================================
 * Links / metadata
 * ============================================================================ */

static int sys_link(const char *user_old, const char *user_new)
{
    char old_path[MAX_PATH_LEN], new_path[MAX_PATH_LEN];
    if (copy_path_from_user(user_old, old_path, sizeof(old_path)) < 0)
        return -EFAULT;
    if (copy_path_from_user(user_new, new_path, sizeof(new_path)) < 0)
        return -EFAULT;
    int r = fs_access_perm(old_path, 2);
    if (r < 0)
        return r;
    if (fs_link(old_path, new_path) < 0)
        return -EEXIST;
    return 0;
}

static int sys_symlink(const char *user_target, const char *user_path)
{
    char target[MAX_PATH_LEN], path[MAX_PATH_LEN];
    if (copy_path_from_user(user_target, target, sizeof(target)) < 0)
        return -EFAULT;
    if (copy_path_from_user(user_path, path, sizeof(path)) < 0)
        return -EFAULT;
    if (fs_symlink(target, path) < 0)
        return -EEXIST;
    return 0;
}

static int sys_readlink(const char *user_path, char *user_buf, size_t bufsize)
{
    char path[MAX_PATH_LEN];
    if (copy_path_from_user(user_path, path, sizeof(path)) < 0)
        return -EFAULT;
    if (!valid_user_pointer(user_buf, bufsize))
        return -EFAULT;

    char kbuf[256];
    size_t n = bufsize > sizeof(kbuf) ? sizeof(kbuf) : bufsize;
    int r = fs_readlink(path, kbuf, n);
    if (r < 0)
        return -EINVAL;
    if (copy_to_user(user_buf, kbuf, (size_t)r + 1) < 0)
        return -EFAULT;
    return r;
}

static int sys_lstat(const char *user_path, stat_t *user_stat)
{
    /* Symlinks are not followed anywhere in this kernel yet, so lstat
     * and stat behave identically. */
    return sys_stat(user_path, user_stat);
}

static int sys_chmod(const char *user_path, uint32_t mode)
{
    char path[MAX_PATH_LEN];
    if (copy_path_from_user(user_path, path, sizeof(path)) < 0)
        return -EFAULT;
    int r = fs_access_perm(path, 2);
    if (r < 0)
        return r;
    if (fs_chmod(path, mode) < 0)
        return -ENOENT;
    return 0;
}

static int sys_fchmod(int fd, uint32_t mode)
{
    if (fs_fchmod(fd, mode) < 0)
        return -EBADF;
    return 0;
}

static int sys_mknod(const char *user_path, uint32_t mode, uint32_t dev)
{
    char path[MAX_PATH_LEN];
    if (copy_path_from_user(user_path, path, sizeof(path)) < 0)
        return -EFAULT;
    if (fs_mknod(path, mode, dev) < 0)
        return -EPERM;
    return 0;
}

/* ============================================================================
 * Time syscalls (clock_gettime, times, itimers, utime)
 * ============================================================================ */

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2

#define ITIMER_REAL 0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF 2

/* POSIX tms (must match crt/include/sys/times.h) */
struct tms_k {
    int32_t tms_utime;
    int32_t tms_stime;
    int32_t tms_cutime;
    int32_t tms_cstime;
};

/* POSIX itimerval (must match crt/include/sys/time.h) */
struct itimerval_k {
    timeval_t it_interval;
    timeval_t it_value;
};

/* POSIX utimbuf (must match crt/include/utime.h) */
struct utimbuf_k {
    int32_t actime;
    int32_t modtime;
};

/* Ticks -> timespec. The PIT runs at 100 Hz (10 ms/tick). */
static void ticks_to_timespec(int32_t *sec, int32_t *nsec, uint32_t ticks)
{
    *sec  = (int32_t)(ticks / 100);
    *nsec = (int32_t)((ticks % 100) * 10000000);
}

/* timespec -> ticks, rounding up so a nonzero request sleeps at least 1 tick */
static uint32_t timespec_to_ticks(int32_t sec, int32_t nsec)
{
    uint32_t t = (uint32_t)sec * 100 + (uint32_t)nsec / 10000000;
    if (sec > 0 || nsec > 0) {
        if ((nsec % 10000000) != 0)
            t++;
    }
    return t;
}

static int sys_clock_gettime(int clockid, timespec_t *user_ts)
{
    if (!valid_user_pointer(user_ts, sizeof(timespec_t)))
        return -EFAULT;

    timespec_t ts;
    uint32_t ticks = pit_get_ticks();

    switch (clockid) {
        case CLOCK_REALTIME: {
            /* Wall clock = boot time + uptime.  Compute separately to
             * avoid overflowing 32-bit ticks with boot_time * 100. */
            ticks_to_timespec(&ts.tv_sec, &ts.tv_nsec, ticks);
            ts.tv_sec += (int32_t)rtc_get_boot_unix_time();
            if (copy_to_user(user_ts, &ts, sizeof(timespec_t)) < 0)
                return -EFAULT;
            return 0;
        }
        case CLOCK_MONOTONIC:
            break;
        case CLOCK_PROCESS_CPUTIME_ID:
            ticks = current->uticks + current->sticks;
            break;
        default:
            return -EINVAL;
    }

    ticks_to_timespec(&ts.tv_sec, &ts.tv_nsec, ticks);
    if (copy_to_user(user_ts, &ts, sizeof(timespec_t)) < 0)
        return -EFAULT;
    return 0;
}

static int sys_clock_getres(int clockid, timespec_t *user_ts)
{
    timespec_t ts = { 0, 10000000 };   /* 10 ms resolution */

    switch (clockid) {
        case CLOCK_REALTIME:
        case CLOCK_MONOTONIC:
        case CLOCK_PROCESS_CPUTIME_ID:
            break;
        default:
            return -EINVAL;
    }

    if (user_ts) {
        if (!valid_user_pointer(user_ts, sizeof(timespec_t)))
            return -EFAULT;
        if (copy_to_user(user_ts, &ts, sizeof(timespec_t)) < 0)
            return -EFAULT;
    }
    return 0;
}

static int sys_times(struct tms_k *user_tms)
{
    if (!valid_user_pointer(user_tms, sizeof(struct tms_k)))
        return -EFAULT;

    struct tms_k tms;
    tms.tms_utime  = (int32_t)current->uticks;
    tms.tms_stime  = (int32_t)current->sticks;
    tms.tms_cutime = 0;   /* no per-child accounting yet */
    tms.tms_cstime = 0;

    if (copy_to_user(user_tms, &tms, sizeof(struct tms_k)) < 0)
        return -EFAULT;
    return (int)pit_get_ticks();   /* return clock ticks since boot */
}

static int sys_alarm(uint32_t seconds)
{
    uint32_t old_ticks = current->itimer_remaining;
    uint32_t old_secs  = (old_ticks + 99) / 100;   /* round up */

    current->itimer_remaining = seconds * 100;
    current->itimer_interval  = 0;
    return (int)old_secs;
}

/* ============================================================================
 * Filesystem mount/umount/sync
 * ============================================================================ */

static int sys_mount(const char *user_source, const char *user_target,
                     const char *user_fstype, unsigned long flags,
                     const void *data)
{
    (void)flags; (void)data;

    /* Only root may mount. */
    if (current->euid != 0)
        return -EPERM;

    char source[MAX_PATH_LEN], target[MAX_PATH_LEN], fstype[16];
    if (copy_path_from_user(user_source, source, sizeof(source)) < 0)
        return -EFAULT;
    if (copy_path_from_user(user_target, target, sizeof(target)) < 0)
        return -EFAULT;
    if (copy_path_from_user(user_fstype, fstype, sizeof(fstype)) < 0)
        return -EFAULT;

    /* The mount target must exist and be a directory. */
    stat_t st;
    if (fs_stat(target, &st) < 0)
        return -ENOENT;
    if (st.type != DT_DIR)
        return -ENOTDIR;

    if (fs_is_mounted(target))
        return -EBUSY;

    int pseudo = fs_is_pseudo_fs(fstype);
    if (pseudo < 0)
        return -ENODEV;   /* unknown filesystem type */

    int dev_id = -1, minor = -1;
    if (!pseudo) {
        /* Block-backed filesystem: resolve the source to a block device
         * via devfs.  The source is a path like "/dev/hdb2"; devfs nodes
         * are named without the "/dev/". */
        extern int devfs_resolve(const char *path, uint8_t *type,
                                 int *dev_id, int *minor);
        const char *devname = source;
        while (*devname == '/')
            devname++;
        if (strncmp(devname, "dev/", 4) == 0)
            devname += 4;

        uint8_t dtype;
        if (devfs_resolve(devname, &dtype, &dev_id, &minor) < 0 ||
            dtype != DT_BLKDEV)
            return -ENODEV;
    }

    if (fs_mount(dev_id, minor, fstype, target) < 0)
        return -EINVAL;
    return 0;
}

int sys_umount(const char *user_target)
{
    if (current->euid != 0)
        return -EPERM;

    char target[MAX_PATH_LEN];
    if (copy_path_from_user(user_target, target, sizeof(target)) < 0)
        return -EFAULT;

    if (fs_unmount(target) < 0)
        return -EINVAL;
    return 0;
}

int sys_sync(void)
{
    extern int cache_flush(void);
    cache_flush();
    return 0;
}

/* Accessors for the per-timer kernel fields. */
static uint32_t *timer_remaining_ptr(int which)
{
    switch (which) {
        case ITIMER_REAL:    return &current->itimer_remaining;
        case ITIMER_VIRTUAL: return &current->vtimer_remaining;
        case ITIMER_PROF:    return &current->ptimer_remaining;
        default:             return NULL;
    }
}

static uint32_t *timer_interval_ptr(int which)
{
    switch (which) {
        case ITIMER_REAL:    return &current->itimer_interval;
        case ITIMER_VIRTUAL: return &current->vtimer_interval;
        case ITIMER_PROF:    return &current->ptimer_interval;
        default:             return NULL;
    }
}

static int sys_setitimer(int which, struct itimerval_k *user_new,
                         struct itimerval_k *user_old)
{
    uint32_t *rem = timer_remaining_ptr(which);
    uint32_t *ivl = timer_interval_ptr(which);
    if (!rem || !ivl)
        return -EINVAL;

    if (user_old) {
        if (!valid_user_pointer(user_old, sizeof(struct itimerval_k)))
            return -EFAULT;
        struct itimerval_k old;
        old.it_value.tv_sec  = (int32_t)(*rem / 100);
        old.it_value.tv_usec = (int32_t)((*rem % 100) * 10000);
        old.it_interval.tv_sec  = (int32_t)(*ivl / 100);
        old.it_interval.tv_usec = (int32_t)((*ivl % 100) * 10000);
        if (copy_to_user(user_old, &old, sizeof(struct itimerval_k)) < 0)
            return -EFAULT;
    }

    if (user_new) {
        if (!valid_user_pointer(user_new, sizeof(struct itimerval_k)))
            return -EFAULT;
        struct itimerval_k nv;
        if (copy_from_user(&nv, user_new, sizeof(struct itimerval_k)) < 0)
            return -EFAULT;

        if (nv.it_value.tv_sec == 0 && nv.it_value.tv_usec == 0) {
            /* Disarm the timer. */
            *rem = 0;
        } else {
            *rem = timespec_to_ticks(nv.it_value.tv_sec, nv.it_value.tv_usec);
        }
        if (nv.it_interval.tv_sec == 0 && nv.it_interval.tv_usec == 0)
            *ivl = 0;
        else
            *ivl = timespec_to_ticks(nv.it_interval.tv_sec, nv.it_interval.tv_usec);
    }

    return 0;
}

static int sys_getitimer(int which, struct itimerval_k *user_old)
{
    uint32_t *rem = timer_remaining_ptr(which);
    uint32_t *ivl = timer_interval_ptr(which);
    if (!rem || !ivl)
        return -EINVAL;
    if (!valid_user_pointer(user_old, sizeof(struct itimerval_k)))
        return -EFAULT;

    struct itimerval_k old;
    old.it_value.tv_sec  = (int32_t)(*rem / 100);
    old.it_value.tv_usec = (int32_t)((*rem % 100) * 10000);
    old.it_interval.tv_sec  = (int32_t)(*ivl / 100);
    old.it_interval.tv_usec = (int32_t)((*ivl % 100) * 10000);

    if (copy_to_user(user_old, &old, sizeof(struct itimerval_k)) < 0)
        return -EFAULT;
    return 0;
}

static int sys_utime(const char *user_path, struct utimbuf_k *user_times)
{
    char path[MAX_PATH_LEN];
    if (copy_path_from_user(user_path, path, sizeof(path)) < 0)
        return -EFAULT;

    uint32_t atime, mtime;
    if (user_times) {
        if (!valid_user_pointer(user_times, sizeof(struct utimbuf_k)))
            return -EFAULT;
        struct utimbuf_k ut;
        if (copy_from_user(&ut, user_times, sizeof(struct utimbuf_k)) < 0)
            return -EFAULT;
        atime = (uint32_t)ut.actime;
        mtime = (uint32_t)ut.modtime;
    } else {
        uint32_t now = rtc_get_boot_unix_time() + pit_get_ticks() / 100;
        atime = mtime = now;
    }

    int r = fs_access_perm(path, 2);   /* must be writable */
    if (r < 0)
        return r;
    if (fs_utime(path, atime, mtime) < 0)
        return -ENOENT;
    return 0;
}

/* ============================================================================
 * System Call Dispatcher
 * ============================================================================ */

/**
 * Main syscall dispatcher - called from isr128 (int 0x80)
 */
int syscall_handler(registers_t *regs, int num, uint32_t arg1, uint32_t arg2,
                    uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6)
{
    (void)arg6;

    switch (num) {
        case SYS_EXIT:
            sys_exit((int)arg1);
            return 0;   /* never returns */

        case SYS_FORK:
            return sys_fork(regs);

        case SYS_READ:
            return sys_read((int)arg1, (char *)arg2, (size_t)arg3);

        case SYS_WRITE:
            return sys_write((int)arg1, (const char *)arg2, (size_t)arg3);

        case SYS_OPEN:
            return sys_open((const char *)arg1, (int)arg2, (uint32_t)arg3);

        case SYS_CLOSE:
            return sys_close((int)arg1);

        case SYS_WAITPID:
            return sys_wait4((int)arg1, (int *)arg2, (int)arg3, NULL);

        case SYS_CREAT:
            return sys_open((const char *)arg1, O_CREAT | O_WRONLY | O_TRUNC,
                            (uint32_t)arg2);

        case SYS_LINK:
            return sys_link((const char *)arg1, (const char *)arg2);

        case SYS_UNLINK:
            return sys_unlink((const char *)arg1);

        case SYS_EXECVE:
            return sys_execve((const char *)arg1, (char **)arg2, (char **)arg3);

        case SYS_CHDIR:
            return sys_chdir((const char *)arg1);

        case SYS_TIME:
            return sys_time((int *)arg1);

        case SYS_MKNOD:
            return sys_mknod((const char *)arg1, arg2, arg3);

        case SYS_CHMOD:
            return sys_chmod((const char *)arg1, arg2);

        case SYS_LCHOWN:
            return sys_lchown((const char *)arg1, arg2, arg3);

        case SYS_LSEEK:
            return sys_lseek((int)arg1, (int32_t)arg2, (int)arg3);

        case SYS_GETPID:
            return sys_getpid();

        case SYS_MOUNT:
            return sys_mount((const char *)arg1, (const char *)arg2,
                             (const char *)arg3, (unsigned long)arg4,
                             (const void *)arg5);

        case SYS_UMOUNT:
            return sys_umount((const char *)arg1);

        case SYS_SETUID:
            return sys_setuid(arg1);

        case SYS_GETUID:
            return sys_getuid();

        case SYS_ALARM:
            return sys_alarm(arg1);

        case SYS_ACCESS:
            return sys_access((const char *)arg1, (int)arg2);

        case SYS_NICE:
            return sys_nice((int)arg1);

        case SYS_NANOSLEEP:
            return sys_nanosleep((timespec_t *)arg1, (timespec_t *)arg2);

        case SYS_SYNC:
            return sys_sync();

        case SYS_KILL:
            return sys_kill((int)arg1, (int)arg2);

        case SYS_RENAME:
            return sys_rename((const char *)arg1, (const char *)arg2);

        case SYS_MKDIR:
            return sys_mkdir((const char *)arg1, (uint32_t)arg2);

        case SYS_RMDIR:
            return sys_rmdir((const char *)arg1);

        case SYS_DUP:
            return sys_dup((int)arg1);

        case SYS_PIPE:
            return sys_pipe((int *)arg1);

        case SYS_TIMES:
            return sys_times((struct tms_k *)arg1);

        case SYS_BRK:
            return sys_brk(arg1);

        case SYS_SETGID:
            return sys_setgid(arg1);

        case SYS_GETGID:
            return sys_getgid();

        case SYS_SIGNAL:
            return sys_signal((int)arg1, (uint32_t)arg2);

        case SYS_GETEUID:
            return sys_geteuid();

        case SYS_GETEGID:
            return sys_getegid();

        case SYS_UMOUNT2:
            return sys_umount2((const char *)arg1, (int)arg2);

        case SYS_IOCTL:
            return sys_ioctl((int)arg1, arg2, arg3);

        case SYS_FCNTL:
            return sys_fcntl((int)arg1, (int)arg2, arg3);

        case SYS_SETPGID:
            return sys_setpgid((int)arg1, (int)arg2);

        case SYS_UMASK:
            return sys_umask(arg1);

        case SYS_DUP2:
            return sys_dup2((int)arg1, (int)arg2);

        case SYS_GETPPID:
            return sys_getppid();

        case SYS_GETPGRP:
            return sys_getpgrp();

        case SYS_SETSID:
            return sys_setsid();

        case SYS_SIGACTION:
            return sys_sigaction((int)arg1, (sigaction_t *)arg2,
                                 (sigaction_t *)arg3);

        case SYS_SETREUID:
            return sys_setreuid(arg1, arg2);

        case SYS_SETREGID:
            return sys_setregid(arg1, arg2);

        case SYS_SIGSUSPEND:
            return sys_sigsuspend((sigset_t *)arg1);

        case SYS_SIGPENDING:
            return sys_sigpending((sigset_t *)arg1);

        case SYS_SETHOSTNAME:
            return sys_sethostname((const char *)arg1, (int)arg2);

        case SYS_SETRLIMIT:
            return sys_setrlimit((int)arg1, (void *)arg2);

        case SYS_GETRLIMIT:
            return sys_getrlimit((int)arg1, (void *)arg2);

        case SYS_GETRUSAGE:
            return sys_getrusage((int)arg1, (void *)arg2);

        case SYS_GETTIMEOFDAY:
            return sys_gettimeofday((struct timeval *)arg1, (void *)arg2);

        case SYS_SETTIMEOFDAY:
            return sys_settimeofday((struct timeval *)arg1, (void *)arg2);

        case SYS_CHROOT:
            return sys_chroot((const char *)arg1);

        case SYS_FLOCK:
            return sys_flock((int)arg1, (int)arg2);

        case SYS_SYSINFO:
            return sys_sysinfo((void *)arg1);

        case SYS_GETGROUPS:
            return sys_getgroups((int)arg1, (void *)arg2);

        case SYS_SETGROUPS:
            return sys_setgroups((int)arg1, (void *)arg2);

        case SYS_SELECT:
            return sys_select((int)arg1, (void *)arg2, (void *)arg3,
                              (void *)arg4, (void *)arg5);

        case SYS_SYMLINK:
            return sys_symlink((const char *)arg1, (const char *)arg2);

        case SYS_READLINK:
            return sys_readlink((const char *)arg1, (char *)arg2, (size_t)arg3);

        case SYS_REBOOT:
            return sys_reboot(arg1, arg2, arg3, (void *)arg4);

        case SYS_MMAP:
            return sys_mmap(arg1, (size_t)arg2, (int)arg3,
                            (int)arg4, (int)arg5, arg6);

        /* Linux i386 mmap2: the 6th argument is a page-granular offset. */
        case SYS_MMAP2:
            return sys_mmap(arg1, (size_t)arg2, (int)arg3,
                            (int)arg4, (int)arg5, arg6 * 4096U);

        case SYS_MUNMAP:
            return sys_munmap(arg1, (size_t)arg2);

        case SYS_TRUNCATE:
            return sys_truncate((const char *)arg1, (uint32_t)arg2);

        case SYS_FTRUNCATE:
            return sys_ftruncate((int)arg1, arg2);

        case SYS_FCHMOD:
            return sys_fchmod((int)arg1, arg2);

        case SYS_FCHOWN:
            return sys_fchown((int)arg1, arg2, arg3);

        case SYS_GETPRIORITY:
            return sys_getpriority((int)arg1, (int)arg2);

        case SYS_SETPRIORITY:
            return sys_setpriority((int)arg1, (int)arg2, (int)arg3);

        case SYS_STATFS:
            return sys_statfs((const char *)arg1, (void *)arg2);

        case SYS_FSTATFS:
            return sys_fstatfs((int)arg1, (void *)arg2);

        case SYS_SETITIMER:
            return sys_setitimer((int)arg1, (struct itimerval_k *)arg2,
                                 (struct itimerval_k *)arg3);

        case SYS_GETITIMER:
            return sys_getitimer((int)arg1, (struct itimerval_k *)arg2);

        case SYS_STAT:
            return sys_stat((const char *)arg1, (stat_t *)arg2);

        case SYS_LSTAT:
            return sys_lstat((const char *)arg1, (stat_t *)arg2);

        case SYS_FSTAT:
            return sys_fstat((int)arg1, (stat_t *)arg2);

        case SYS_WAIT4:
            return sys_wait4((int)arg1, (int *)arg2, (int)arg3, (void *)arg4);

        case SYS_FSYNC:
            return sys_fsync((int)arg1);

        case SYS_SIGRETURN:
            return sys_sigreturn(regs);

        case SYS_SETDOMAINNAME:
            return sys_setdomainname((const char *)arg1, (int)arg2);

        case SYS_UNAME:
            return sys_uname((struct utsname *)arg1);

        case SYS_MPROTECT:
            return sys_mprotect(arg1, (size_t)arg2, (int)arg3);

        case SYS_SIGPROCMASK:
            return sys_sigprocmask((int)arg1, (sigset_t *)arg2,
                                   (sigset_t *)arg3);

        case SYS_GETPGID:
            return sys_getpgid((int)arg1);

        case SYS_FCHDIR:
            return sys_fchdir((int)arg1);

        case SYS_PERSONALITY:
            return sys_personality(arg1);

        case SYS_UTIME:
            return sys_utime((const char *)arg1, (struct utimbuf_k *)arg2);

        case SYS_SETFSUID:
            return sys_setfsuid(arg1);

        case SYS_SETFSGID:
            return sys_setfsgid(arg1);

        case SYS_GETDENTS:
            return sys_getdents((int)arg1, (dirent_t *)arg2, (unsigned int)arg3);

        case SYS_READV:
            return sys_readv((int)arg1, (struct iovec_k *)arg2, (int)arg3);

        case SYS_WRITEV:
            return sys_writev((int)arg1, (struct iovec_k *)arg2, (int)arg3);

        case SYS_GETSID:
            return sys_getsid((int)arg1);

        case SYS_FDATASYNC:
            return sys_fdatasync((int)arg1);

        case SYS_MLOCK:
            return sys_mlock(arg1, (size_t)arg2);

        case SYS_MUNLOCK:
            return sys_munlock(arg1, (size_t)arg2);

        case SYS_MLOCKALL:
            return sys_mlockall((int)arg1);

        case SYS_MUNLOCKALL:
            return sys_munlockall();

        case SYS_SCHED_SETPARAM:
            return sys_sched_setparam((int)arg1, (void *)arg2);

        case SYS_SCHED_GETPARAM:
            return sys_sched_getparam((int)arg1, (void *)arg2);

        case SYS_SCHED_GETSCHEDULER:
            return sys_sched_getscheduler((int)arg1);

        case SYS_SCHED_YIELD:
            return sys_sched_yield();

        case SYS_SCHED_GET_PRIORITY_MAX:
            return sys_sched_get_priority_max((int)arg1);

        case SYS_SCHED_GET_PRIORITY_MIN:
            return sys_sched_get_priority_min((int)arg1);

        case SYS_SCHED_RR_GET_INTERVAL:
            return sys_sched_rr_get_interval((int)arg1, (void *)arg2);

        case SYS_POLL:
            return sys_poll((struct pollfd_k *)arg1, (int)arg2, (int)arg3);

        case SYS_PRCTL:
            return sys_prctl((int)arg1, arg2, arg3, arg4, arg5);

        case SYS_PREAD:
            return sys_pread((int)arg1, (void *)arg2, (size_t)arg3, arg4);

        case SYS_PWRITE:
            return sys_pwrite((int)arg1, (const void *)arg2, (size_t)arg3, arg4);

        case SYS_GETCWD:
            return sys_getcwd((char *)arg1, (size_t)arg2);

        case SYS_SENDFILE:
            return sys_sendfile((int)arg1, (int)arg2, (void *)arg3, (size_t)arg4);

        case SYS_FADVISE64:
            return sys_fadvise64((int)arg1, arg2, arg3, (int)arg4);

        case SYS_EXIT_GROUP:
            return sys_exit_group((int)arg1);

        case SYS_SET_TID_ADDRESS:
            return sys_set_tid_address((void *)arg1);

        case SYS_CLOCK_GETTIME:
            return sys_clock_gettime((int)arg1, (timespec_t *)arg2);

        case SYS_CLOCK_GETRES:
            return sys_clock_getres((int)arg1, (timespec_t *)arg2);

        case SYS_CLOCK_NANOSLEEP:
            return sys_clock_nanosleep((int)arg1, (int)arg2,
                                       (void *)arg3, (void *)arg4);

        case SYS_TGKILL:
            return sys_tgkill((int)arg1, (int)arg2, (int)arg3);

        case SYS_UTIMES:
            return sys_utimes((const char *)arg1, (void *)arg2);

        case SYS_SYNCFS:
            return sys_syncfs((int)arg1);

        case SYS_GETCPU:
            return sys_getcpu((void *)arg1, (void *)arg2, (void *)arg3);

        case SYS_DUP3:
            return sys_dup3((int)arg1, (int)arg2, (int)arg3);

        case SYS_PIPE2:
            return sys_pipe2((int *)arg1, (int)arg2);

        case SYS_MEMBARRIER:
            return sys_membarrier((int)arg1, (int)arg2);

        case SYS_GETRANDOM:
            return sys_getrandom((void *)arg1, (size_t)arg2, (unsigned int)arg3);

        case SYS_CLOSE_RANGE:
            return sys_close_range(arg1, arg2, (int)arg3);

        case SYS_SETRESUID:
            return sys_setresuid(arg1, arg2, arg3);

        case SYS_GETRESUID:
            return sys_getresuid((void *)arg1, (void *)arg2, (void *)arg3);

        case SYS_SETRESGID:
            return sys_setresgid(arg1, arg2, arg3);

        case SYS_GETRESGID:
            return sys_getresgid((void *)arg1, (void *)arg2, (void *)arg3);

        case SYS_CHOWN:
            return sys_chown((const char *)arg1, arg2, arg3);

        default:
            printk("[SYSCALL] Unknown syscall number: %d\n", num);
            return -ENOSYS;
    }
}
