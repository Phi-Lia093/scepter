/* ============================================================================
 * System Call Implementation
 * ============================================================================ */

#include "kernel/syscall.h"
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
    
    printk("[SYSCALL] open(\"%s\", 0x%x)\n", kernel_path, flags);
    
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
    
    printk("[SYSCALL] write(fd=%d, buf=0x%08x, count=%u)\n",
           fd, (uint32_t)user_buf, count);
    
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
    
    printk("[SYSCALL] write: wrote %u bytes\n", total_written);
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
    printk("[SYSCALL] close(fd=%d)\n", fd);
    return fs_close(fd);
}

/* ============================================================================
 * System Call Dispatcher
 * ============================================================================ */

/**
 * Main syscall dispatcher - called from isr128 (int 0x80)
 */
int syscall_handler(int num, uint32_t arg1, uint32_t arg2,
                    uint32_t arg3, uint32_t arg4, uint32_t arg5)
{
    (void)arg4;  /* Unused */
    (void)arg5;  /* Unused */
    
    printk("[SYSCALL] num=%d arg1=0x%08x arg2=0x%08x arg3=0x%08x %08x %08x\n",
           num, arg1, arg2, arg3, arg4, arg5);
    
    switch (num) {
        case SYS_OPEN:
            return sys_open((const char *)arg1, (int)arg2);
        
        case SYS_WRITE:
            return sys_write((int)arg1, (const char *)arg2, (size_t)arg3);
        
        case SYS_READ:
            return sys_read((int)arg1, (char *)arg2, (size_t)arg3);
        
        case SYS_CLOSE:
            return sys_close((int)arg1);
        
        default:
            printk("[SYSCALL] Unknown syscall number: %d\n", num);
            return -1;
    }
}