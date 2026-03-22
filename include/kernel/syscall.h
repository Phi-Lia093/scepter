#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * CPU Register State (for fork context preservation)
 * ========================================================================= */

typedef struct registers {
    /* Pushed by pusha */
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    
    /* Segment selectors */
    uint32_t gs, fs, es, ds;
    
    /* Saved CR3 */
    uint32_t cr3;
    
    /* IRET frame (pushed by CPU) */
    uint32_t eip, cs, eflags, user_esp, ss;
} registers_t;

/* =========================================================================
 * System Call Numbers (Linux-compatible subset)
 * ========================================================================= */

#define SYS_EXIT   1
#define SYS_FORK   2
#define SYS_READ   3
#define SYS_WRITE  4
#define SYS_OPEN   5
#define SYS_CLOSE  6
#define SYS_WAIT4  7    /* Simplified as SYS_WAIT */
#define SYS_EXECVE 11   /* Simplified as SYS_EXEC */
#define SYS_LSEEK  19   /* Reposition file offset */
#define SYS_GETPID 20   /* Get process ID */
#define SYS_DUP    41   /* Duplicate file descriptor */
#define SYS_BRK    45
#define SYS_IOCTL  54   /* I/O control */
#define SYS_DUP2   63   /* Duplicate file descriptor to specific fd */
#define SYS_GETPPID 64  /* Get parent process ID */
#define SYS_MMAP   90   /* Old 32-bit mmap */
#define SYS_MUNMAP 91
#define SYS_FSTAT  108  /* Get file status by fd */
#define SYS_STAT   106  /* Get file status by path */
#define SYS_GETCWD 183  /* Get current working directory */

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
 * @param regs Pointer to saved register state
 * @param num Syscall number (from EAX)
 * @param arg1 First argument (from EBX)
 * @param arg2 Second argument (from ECX)
 * @param arg3 Third argument (from EDX)
 * @param arg4 Fourth argument (from ESI)
 * @param arg5 Fifth argument (from EDI)
 * @return Syscall return value (stored in EAX)
 */
int syscall_handler(registers_t *regs, int num, uint32_t arg1, uint32_t arg2,
                    uint32_t arg3, uint32_t arg4, uint32_t arg5)
    __attribute__((cdecl));

#endif /* SYSCALL_H */