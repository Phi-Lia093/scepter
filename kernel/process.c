/* ============================================================================
 * Process Management - exit, fork, exec, wait
 * ============================================================================ */

#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/sched.h"
#include "kernel/exec.h"
#include "arch/cpu.h"
#include "arch/paging.h"
#include "arch/timer.h"
#include "mm/mm.h"
#include "mm/buddy.h"
#include "mm/slab.h"
#include "mm/vma.h"
#include "fs/fs.h"
#include "lib/printk.h"
#include "errno.h"
#include "lib/string.h"

/* ============================================================================
 * Process Termination (exit)
 * ============================================================================ */

/**
 * do_exit - Terminate the current process (shared core).
 * @param status Exit status code
 *
 * Never returns. The process transitions to ZOMBIE state and is reaped by
 * its parent via wait().  Called both from the exit() syscall and from the
 * kill-on-fault / default-signal paths.
 */
void do_exit(int status)
{
    task_struct_t *task = current;
    
    /* Encode the wait status (POSIX layout):
     *   - normal exit(code)          -> (code & 0xff) << 8
     *   - signal death (128+sig in)  -> sig in the low 7 bits
     * Signal numbers are all < 32, so 128+sig is in [129,159]. */
    if (status >= 128)
        task->exit_code = (status - 128) & 0x7f;
    else
        task->exit_code = (status & 0xff) << 8;
    
    /* Declare loop variables once for all uses */
    list_head_t *pos, *tmp;

    /* Write back MAP_SHARED file-backed pages while fds are still open. */
    extern void mm_writeback_shared(task_struct_t *task);
    mm_writeback_shared(task);
    
    /* Close all open file descriptors (with proper reference counting) */
    if (!list_empty(&task->files)) {
        list_for_each_safe(pos, tmp, &task->files) {
            fd_entry_t *fde = list_entry(pos, fd_entry_t, node);
            if (fde) {
                /* fs_close handles refcounting and closes file if last ref */
                fs_close(fde->fd);
            }
        }
    }
    
    /* Free user memory (pages, page tables, VMAs) */
    /* Note: We keep the kernel stack and task_struct for parent to reap */
    
    /* Switch to kernel CR3 immediately - we'll map page tables as needed */
    __asm__ volatile("mov %0, %%cr3" : : "r"(arch_kernel_pgdir_phys()));
    
    /* Free all VMAs and their pages */
    list_for_each_safe(pos, tmp, &task->mm.vma_list) {
        vma_t *vma = list_entry(pos, vma_t, list);
        
        /* Free all pages mapped in this VMA (arch walks the page tables) */
        arch_mm_free_user_pages(task, vma->vm_start, vma->vm_end);
        
        vma_destroy(vma);  /* vma_destroy handles list_del internally */
    }
    
    /* Free the user page tables (also clears the user half of pgdir) */
    arch_mm_free_user_tables(task);
    /* Free the page-directory itself (task teardown). */
    arch_mm_free_pgd(task);
    
    /* Reparent children to init (PID 1) or auto-reap them */
    if (!list_empty(&task->children)) {
        
        list_for_each_safe(pos, tmp, &task->children) {
            task_struct_t *child = list_entry(pos, task_struct_t, sibling);
            
            if (child->state == TASK_ZOMBIE) {
                /* Auto-reap zombie children */
                list_del(&child->sibling);
                remove_task(child);
                free_task(child);
            } else {
                /* Reparent to init (PID 1): actually link the child into
                 * init's children list so it can be reaped later */
                task_struct_t *init_task = find_task_by_pid(1);
                list_del(&child->sibling);
                if (init_task && init_task != task) {
                    child->ppid = init_task->pid;
                    list_add_tail(&child->sibling, &init_task->children);
                } else {
                    /* No init yet (or init itself is exiting): orphan it */
                    child->ppid = 1;
                }
            }
        }
    }
    
    /* Transition to ZOMBIE state */
    task->state = TASK_ZOMBIE;
    
    /* Clear the thread ID if set_tid_address() was called (Linux CLONE_*
     * semantics; lets userland detect thread death). */
    if (task->cleartid) {
        uint32_t zero = 0;
        copy_to_user((void *)task->cleartid, &zero, sizeof(zero));
    }
    
    /* Wake up parent if it's waiting in wait() and notify it (SIGCHLD). */
    {
        task_struct_t *parent = find_task_by_pid(task->ppid);
        if (parent && parent != task) {
            send_signal(parent->pid, SIGCHLD);
            wake_up(&parent->wait);
        }
    }
    
    /* Schedule next task (this never returns) */
    schedule();
    
    /* Should never reach here */
    while(1);
}

/**
 * sys_exit - exit() syscall entry point.
 */
void sys_exit(int status)
{
    do_exit(status);
}

/* ============================================================================
 * Process Duplication (fork)
 * ============================================================================ */

/**
 * fork_cleanup_failed - Clean up a partially-built fork child on error
 * Unlinks the child from the parent's children list, frees any user pages
 * that were already copied into the child's page tables, then frees the task.
 */
static void fork_cleanup_failed(task_struct_t *child)
{
    if (!child) return;
    
    /* Unlink from parent's children list (if still linked) */
    if (child->sibling.next != NULL) {
        list_del(&child->sibling);
    }
    
    /* free_task frees user pages, page tables, kernel stack + task struct */
    free_task(child);
}

/**
 * sys_fork - Create a copy of the current process
 * @param regs CPU register state from syscall entry
 * @return Child PID to parent, 0 to child, -1 on error
 */
int sys_fork(registers_t *regs)
{
    task_struct_t *parent = current;
    
    /* Allocate new task structure */
    task_struct_t *child = alloc_task();
    if (!child) {
        return -ENOMEM;
    }
    
    /* Copy basic fields */
    child->ppid = parent->pid;
    strncpy(child->name, parent->name, sizeof(child->name));
    strncpy(child->cwd, parent->cwd, sizeof(child->cwd));
    strncpy(child->root, parent->root, sizeof(child->root));
    child->next_fd = parent->next_fd;
    
    /* Process identity: children inherit the parent's process group,
     * session, and credentials (POSIX fork semantics). */
    child->pgid = parent->pgid;
    child->sid  = parent->sid;
    child->uid  = parent->uid;
    child->euid = parent->euid;
    child->suid = parent->suid;
    child->gid  = parent->gid;
    child->egid = parent->egid;
    child->sgid = parent->sgid;
    child->fsuid = parent->fsuid;
    child->fsgid = parent->fsgid;
    child->ngroups = parent->ngroups;
    memcpy(child->groups, parent->groups, sizeof(child->groups));
    child->umask = parent->umask;
    child->personality = parent->personality;
    memcpy(child->rlimit_cur, parent->rlimit_cur, sizeof(child->rlimit_cur));
    memcpy(child->rlimit_max, parent->rlimit_max, sizeof(child->rlimit_max));
    child->cleartid = 0;
    child->itimer_remaining = 0;
    child->itimer_interval  = 0;
    child->vtimer_remaining = 0;
    child->vtimer_interval  = 0;
    child->ptimer_remaining = 0;
    child->ptimer_interval  = 0;
    child->uticks = 0;
    child->sticks = 0;
    
    /* Signal state: handlers are inherited, but the child starts with no
     * pending/blocked signals, no handler in flight, and is not stopped. */
    memcpy(child->sig_handlers, parent->sig_handlers, sizeof(child->sig_handlers));
    memcpy(child->sig_hmask,    parent->sig_hmask,    sizeof(child->sig_hmask));
    memcpy(child->sig_hflags,   parent->sig_hflags,   sizeof(child->sig_hflags));
    child->pending    = 0;
    child->blocked    = 0;
    child->sig_active = 0;
    child->stop_sig   = 0;
    child->stop_reported = 0;
    child->continued  = 0;

    /* Inherit scheduling priority */
    child->priority = parent->priority;
    
    /* Add child to parent's children list */
    list_add_tail(&child->sibling, &parent->children);
    
    /* Duplicate file descriptors - share open files with parent */
    list_head_t *pos;
    list_for_each(pos, &parent->files) {
        fd_entry_t *pfd = list_entry(pos, fd_entry_t, node);
        
        /* Allocate new fd_entry for child */
        fd_entry_t *cfd = (fd_entry_t *)kalloc(sizeof(fd_entry_t));
        if (!cfd) {
            printk("[PROCESS] Fork failed: could not duplicate fd_entry\n");
            fork_cleanup_failed(child);
            return -1;
        }
        
        /* Point to the SAME open_file (shared!) */
        cfd->fd = pfd->fd;           /* Same fd number */
        cfd->file = pfd->file;       /* Shared open_file pointer */
        cfd->cloexec = pfd->cloexec; /* FD_CLOEXEC flag is per-fd */
        INIT_LIST_HEAD(&cfd->node);
        
        /* Increment refcount on shared open_file */
        if (cfd->file) {
            cfd->file->refcount++;
        }
        
        /* Add to child's file list */
        list_add_tail(&cfd->node, &child->files);
    }
    
    /* Duplicate memory: VMAs (metadata) */
    list_for_each(pos, &parent->mm.vma_list) {
        vma_t *pvma = list_entry(pos, vma_t, list);
        
        /* Create matching VMA in child */
        vma_t *cvma = vma_create(pvma->vm_start, pvma->vm_end, 
                                 pvma->vm_flags, pvma->vm_type);
        if (!cvma) {
            printk("[PROCESS] Fork failed: could not create child VMA\n");
            fork_cleanup_failed(child);
            return -1;
        }
        cvma->vm_fd       = pvma->vm_fd;
        cvma->vm_file_off = pvma->vm_file_off;
        cvma->vm_shared   = pvma->vm_shared;
        
        vma_insert(child, cvma);
    }
    
    /* Duplicate page tables + pages (eager copy, arch-specific layout). */
    if (arch_mm_copy_user(parent, child) < 0) {
        printk("[PROCESS] Fork failed: out of memory copying pages\n");
        fork_cleanup_failed(child);
        return -1;
    }
    
    /* Copy memory region info */
    child->mm.code_start = parent->mm.code_start;
    child->mm.code_end = parent->mm.code_end;
    child->mm.brk_start = parent->mm.brk_start;
    child->mm.brk_end = parent->mm.brk_end;
    child->mm.stack_start = parent->mm.stack_start;
    child->mm.stack_end = parent->mm.stack_end;
    child->mm.mmap_base = parent->mm.mmap_base;
    child->mm.mmap_end = parent->mm.mmap_end;
    
    /* Set up child's kernel stack for first execution.
     * When fork returns, the child should get EAX=0 (return value for
     * child) and resume at the same user point as the parent.
     * The arch builds the switch_to() popa/popfl frame + a ring-3 IRET
     * frame cloned from the parent's trap frame (see arch/i386/context.c),
     * so the child iret's back to user mode at the instruction after
     * the syscall. */
    arch_setup_first_stack(child, 0, 0, regs);
    
    /* Add child to scheduler */
    child->state = TASK_READY;
    add_task(child);
    
    /* Return child PID to parent */
    return (int)child->pid;
    
    /* Return child PID to parent */
    return (int)child->pid;
}

/* ============================================================================
 * Wait for Child (wait)
 * ============================================================================ */

/**
 * sys_wait4 - Wait for a child process to exit
 * @param pid   -1: any child; 0: any child in our group (treated as any);
 *               >0: that specific child
 * @param status_ptr User pointer to store exit status (can be NULL)
 * @param options    Ignored for now (0)
 * @param rusage     Ignored (NULL)
 * @return Child PID on success, -1 on error
 */
int sys_wait4(int pid, int *status_ptr, int options, void *rusage)
{
    task_struct_t *parent = current;

    /* Scan children once: reap a matching zombie, or note a live child. */
    int found_live_child = 0;

    list_head_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &parent->children) {
        task_struct_t *child = list_entry(pos, task_struct_t, sibling);

        if (pid > 0 && child->pid != (uint32_t)pid)
            continue;   /* not the requested child */
        if (pid == 0 && child->pgid != parent->pgid)
            continue;   /* not in the caller's process group */

        if (child->state == TASK_ZOMBIE) {
            /* Found a matching zombie! */
            int cpid = (int)child->pid;
            int status = child->exit_code;

            /* Return status to user if requested */
            if (status_ptr) {
                if (copy_to_user(status_ptr, &status, sizeof(status)) < 0)
                    return -EFAULT;
            }

            /* Remove from children list */
            list_del(&child->sibling);

            /* Remove from scheduler and free task */
            remove_task(child);
            free_task(child);

            /* A reaped child's SIGCHLD is now satisfied; clear it so a
             * later blocking syscall doesn't see a stale pending SIGCHLD
             * and spuriously return -EINTR. */
            parent->pending &= ~(1u << SIGCHLD);

            return cpid;
        }

        /* Stopped child: report once when WUNTRACED is requested. */
        if (child->state == TASK_STOPPED && !child->stop_reported) {
            if (options & WUNTRACED) {
                int status = (child->stop_sig << 8) | 0x7f;
                if (status_ptr) {
                    if (copy_to_user(status_ptr, &status, sizeof(status)) < 0)
                        return -EFAULT;
                }
                child->stop_reported = 1;
                return (int)child->pid;
            }
            found_live_child = 1;
            continue;
        }

        /* Continued after a stop: report once when WCONTINUED is requested. */
        if (child->continued && (options & WCONTINUED)) {
            int status = 0xffff;
            if (status_ptr) {
                if (copy_to_user(status_ptr, &status, sizeof(status)) < 0)
                    return -EFAULT;
            }
            child->continued = 0;
            return (int)child->pid;
        }

        found_live_child = 1;
    }

    /* No matching child at all: ECHILD. */
    if (!found_live_child)
        return -ECHILD;

    /* WNOHANG: report "no state change" without blocking. */
    if (options & WNOHANG)
        return 0;

    /* Matching children exist but none are zombies yet - block until a
     * child exits (sys_exit wakes us) or stops (do_signal wakes us).
     * Then re-scan. */
    sleep_on(&parent->wait);
    return sys_wait4(pid, status_ptr, options, rusage);
}

/* Backwards-compatible wait(): wait for any child. */
int sys_wait(int *status_ptr)
{
    return sys_wait4(-1, status_ptr, 0, NULL);
}

/* ============================================================================
 * Sleep (nanosleep)
 * ============================================================================ */

/**
 * sys_nanosleep - Sleep for a specified duration
 * @param user_req User pointer to timespec (seconds + nanoseconds)
 * @param user_rem User pointer to store remaining time (may be NULL)
 * @return 0 on success, -1 on error
 *
 * The PIT runs at 100 Hz, so each tick is 10 ms. Nanoseconds are rounded
 * UP to whole ticks; a zero duration returns immediately.
 */
int sys_nanosleep(timespec_t *user_req, timespec_t *user_rem)
{
    timespec_t req;
    
    /* Validate and copy the request from userspace */
    if (!user_req || !valid_user_pointer(user_req, sizeof(timespec_t))) {
        printk("[TIME] nanosleep: bad req pointer 0x%08x\n", (uint32_t)user_req);
        return -EFAULT;
    }
    if (copy_from_user(&req, user_req, sizeof(req)) < 0) {
        return -EFAULT;
    }
    if (req.tv_sec < 0 || req.tv_nsec < 0 || req.tv_nsec >= 1000000000L) {
        printk("[TIME] nanosleep: invalid time (%d, %d)\n", req.tv_sec, req.tv_nsec);
        return -EINVAL;
    }
    
    /* Convert to PIT ticks (100 Hz): 1 tick = 10 ms */
    uint32_t total_ticks = (uint32_t)req.tv_sec * 100UL;
    total_ticks += (uint32_t)((req.tv_nsec + 9999999L) / 10000000L);
    
    uint32_t target = arch_timer_get_ticks() + total_ticks;
    
    /* Sleep until the target tick. The timer IRQ wakes us each tick; we
     * re-check and go back to sleep until the deadline.  A pending signal
     * aborts the sleep with EINTR. */
    while (arch_timer_get_ticks() < target) {
        sleep_on(&timer_wq);
        if (current->pending)
            return -EINTR;
    }
    
    /* Remaining time is zero (we slept the full requested duration) */
    if (user_rem && valid_user_pointer(user_rem, sizeof(timespec_t))) {
        timespec_t rem = { 0, 0 };
        copy_to_user(user_rem, &rem, sizeof(rem));
    }
    
    return 0;
}

/* ============================================================================
 * Executable argument / environment string handling
 * ============================================================================ */

/**
 * copy_exec_strings - Copy a NULL-terminated user string array into kernel
 * scratch (an exec_strings_t).
 *
 * The caller's argv/envp live in the OLD process image, which exec frees.
 * So they are copied into fixed kernel scratch BEFORE the old image is
 * torn down; setup_initial_stack() packs them into the NEW image's stack.
 */
int copy_exec_strings(char **user_ptrs, exec_strings_t *out)
{
    if (!out) return -1;
    out->count = 0;
    out->data_len = 0;

    if (!user_ptrs)
        return 0;   /* empty array */

    /* Leave room for the terminating NULL in ptrs[] */
    for (uint32_t i = 0; i < EXEC_MAX_ARGS - 1; i++) {
        char *uptr;
        if (copy_from_user(&uptr, &user_ptrs[i], sizeof(uptr)) < 0)
            return -1;
        if (!uptr)
            break;   /* NULL terminator */

        /* Copy the string byte-by-byte (bounded) */
        char tmp[EXEC_MAX_STR];
        uint32_t n = 0;
        while (n < sizeof(tmp) - 1) {
            char c;
            if (copy_from_user(&c, &uptr[n], 1) < 0)
                return -1;
            tmp[n] = c;
            if (c == '\0')
                break;
            n++;
        }
        if (n >= sizeof(tmp) - 1)
            tmp[sizeof(tmp) - 1] = '\0';   /* truncate over-long strings */

        uint32_t len = strlen(tmp) + 1;
        if (out->data_len + len > EXEC_MAX_DATA) {
            printk("[EXEC] arg/env table overflow\n");
            return -1;
        }
        strcpy(&out->data[out->data_len], tmp);
        out->ptrs[out->count] = &out->data[out->data_len];
        out->data_len += len;
        out->count++;
    }
    out->ptrs[out->count] = NULL;
    return 0;
}

/**
 * setup_initial_stack - Build the argc/argv[]/envp[]/strings block for a
 * new process image on its fresh user stack.
 *
 * Layout (low → high addresses):
 *   [esp]        argc
 *   [esp+4]      argv[0..argc-1]
 *   ...          NULL
 *   ...          envp[0..envc-1]
 *   ...          NULL
 *   [top-str]    argv strings + envp strings (packed at the very top)
 *
 * @param stack_pages Direct-mapped base of the 2-page stack allocation
 * @param stack_vaddr User virtual address of stack_pages (0xBFFFE000)
 * @param argv        Kernel argv table (count >= 1)
 * @param envp        Kernel envp table (may be empty)
 * @param esp         Out: initial user ESP pointing at argc
 * @return 0 on success, -1 if the block does not fit
 */
int setup_initial_stack(void *stack_pages, uintptr_t stack_vaddr,
                        exec_strings_t *argv, exec_strings_t *envp,
                        uintptr_t *esp)
{
    uint32_t argc = argv ? argv->count : 0;
    uint32_t envc = envp ? envp->count : 0;
    uint32_t argv_len = argv ? argv->data_len : 0;
    uint32_t envp_len = envp ? envp->data_len : 0;

    uint32_t str_bytes = argv_len + envp_len;
    /* Slots: argc + argv[0..argc-1] + argv-NULL + envp[0..envc-1] + envp-NULL
     *       = 1 + argc + 1 + envc + 1 = argc + envc + 3 slots.
     * Each slot is one user-space pointer (arch-sized). */
    uint32_t ptr_bytes = (uint32_t)sizeof(uintptr_t) * (argc + envc + 3);

    /* Must fit inside the 8KB (2-page) stack mapping */
    if (str_bytes + ptr_bytes + 16 > 8192)
        return -1;

    uintptr_t top = USER_STACK_TOP;            /* user stack top, exclusive */
    uintptr_t str_va = top - str_bytes;        /* strings at the top        */
    uintptr_t esp0  = (str_va - ptr_bytes) & ~(uintptr_t)15U; /* ptr block */

    char *direct = (char *)stack_pages;

    /* Pack the strings (via the direct map) */
    if (argv_len)
        memcpy(direct + (str_va - stack_vaddr), argv->data, argv_len);
    if (envp_len)
        memcpy(direct + (str_va - stack_vaddr) + argv_len,
               envp->data, envp_len);

    /* Build the pointer block (arch-sized slots) */
    uintptr_t *q = (uintptr_t *)(direct + (esp0 - stack_vaddr));
    uint32_t idx = 0;
    q[idx++] = (uintptr_t)argc;
    for (uint32_t i = 0; i < argc; i++)
        q[idx++] = str_va + (uintptr_t)(argv->ptrs[i] - argv->data);
    q[idx++] = 0;                    /* argv NULL terminator */
    for (uint32_t i = 0; i < envc; i++)
        q[idx++] = str_va + argv_len +
                   (uintptr_t)(envp->ptrs[i] - envp->data);
    q[idx++] = 0;                    /* envp NULL terminator */

    if (esp)
        *esp = esp0;
    return 0;
}

/* ============================================================================
 * Process Replacement (exec)
 * ============================================================================ */

/**
 * do_exec - Replace current process with new program (common core)
 * @param user_path User pointer to executable path
 * @param user_argv User pointer to NULL-terminated argv array (may be NULL)
 * @param user_envp User pointer to NULL-terminated envp array (may be NULL)
 * @return -1 on error (does not return on success)
 * 
 * This syscall replaces the current process's memory image with a new executable.
 * It preserves the PID, open file descriptors, and parent relationship.
 * On success, execution continues at the entry point of the new program with
 * argc/argv/envp delivered on the user stack (parsed by crt0.s).
 */
static int do_exec(const char *user_path, char **user_argv, char **user_envp)
{
    task_struct_t *task = current;
    char kernel_path[256];
    exec_strings_t argv = { 0 };
    exec_strings_t envp = { 0 };
    
    /* Validate and copy path from userspace */
    if (!valid_user_pointer(user_path, 1)) {
        printk("[EXEC] Invalid path pointer\n");
        return -EFAULT;
    }
    
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
    
    /* Update the process name to the executable's basename */
    {
        const char *base = kernel_path;
        const char *p = kernel_path;
        while (*p) {
            if (*p == '/')
                base = p + 1;
            p++;
        }
        strncpy(task->name, base, sizeof(task->name) - 1);
        task->name[sizeof(task->name) - 1] = '\0';
    }
    
    /* Copy argv/envp into kernel scratch BEFORE the old image is freed
     * (the caller's argv/envp arrays live in that old image). */
    if (copy_exec_strings(user_argv, &argv) < 0) {
        printk("[EXEC] Bad argv pointer\n");
        return -EFAULT;
    }
    if (copy_exec_strings(user_envp, &envp) < 0) {
        printk("[EXEC] Bad envp pointer\n");
        return -EFAULT;
    }
    
    /* Default argv: {path} when none was supplied */
    if (argv.count == 0) {
        uint32_t plen = strlen(kernel_path) + 1;
        if (plen > EXEC_MAX_DATA)
            return -E2BIG;
        strcpy(argv.data, kernel_path);
        argv.ptrs[0] = argv.data;
        argv.count = 1;
        argv.data_len = plen;
    }
    
    /* Open the executable file */
    int fd = fs_open(kernel_path, O_RDONLY, 0);
    if (fd < 0) {
        printk("[EXEC] Failed to open file\n");
        return -ENOENT;
    }
    
    /* Get file size */
    int file_size_tmp = fs_seek(fd, 0, SEEK_END);
    if (file_size_tmp <= 0) {
        printk("[EXEC] Invalid file size: %d (path=%s fd=%d)\n",
               file_size_tmp, kernel_path, fd);
        fs_close(fd);
        return -ENOEXEC;
    }
    fs_seek(fd, 0, SEEK_SET);
    uint32_t file_size = (uint32_t)file_size_tmp;
    
    /* Pre-flight format check BEFORE tearing down the old image, so a bad
     * executable (malformed ELF, wrong class, ...) fails cleanly without
     * destroying the caller's memory. */
    if (exec_format_check(fd, file_size) < 0) {
        printk("[EXEC] Not a loadable executable: %s\n", kernel_path);
        fs_close(fd);
        return -ENOEXEC;
    }
    
    /* Switch to kernel page tables before freeing user memory */
    __asm__ volatile("mov %0, %%cr3" : : "r"(arch_kernel_pgdir_phys()));
    
    /* Free all existing user memory (VMAs and pages) */
    list_head_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &task->mm.vma_list) {
        vma_t *vma = list_entry(pos, vma_t, list);
        
        /* Free all pages mapped in this VMA (arch walks the page tables) */
        arch_mm_free_user_pages(task, vma->vm_start, vma->vm_end);
        
        vma_destroy(vma);
    }
    
    /* Free all user page tables (also clears the user half of pgdir) */
    arch_mm_free_user_tables(task);
    
    /* exec resets signal state (POSIX): handlers back to default, no
     * pending signals, no handler in flight.  Credentials, process group
     * and session are preserved across exec. */
    memset(task->sig_handlers, 0, sizeof(task->sig_handlers));
    memset(task->sig_hmask,    0, sizeof(task->sig_hmask));
    memset(task->sig_hflags,   0, sizeof(task->sig_hflags));
    task->pending    = 0;
    task->blocked    = 0;
    task->sig_active = 0;
    task->stop_sig   = 0;
    task->stop_reported = 0;
    task->continued  = 0;
    
    /* Load the new image (ELF or legacy flat).  The loader maps all
     * pages/segments and creates the code VMAs. */
    exec_image_t img;
    if (load_binary(task, fd, file_size, &img) < 0) {
        printk("[EXEC] Failed to load image: %s\n", kernel_path);
        fs_close(fd);
        return -ENOEXEC;
    }
    fs_close(fd);
    
    /* Update memory regions from the loader result. */
    task->mm.code_start = img.code_start;
    task->mm.code_end   = img.code_end;
    task->mm.brk_start  = img.brk_start;
    task->mm.brk_end    = img.brk_start;
    
    /* Allocate new user stack (2 pages: [0xBFFFE000, 0xC0000000)).
     * The top page holds the argc/argv/envp block; the lower page gives
     * the new program immediate stack room before demand-paging grows. */
    void *stack_pages = page_alloc(2 * PAGE_SIZE);
    if (!stack_pages) {
        printk("[EXEC] Failed to allocate stack pages\n");
        return -ENOMEM;
    }
    
    uint32_t stack_vaddr = USER_STACK_TOP - 2 * PAGE_SIZE;
    uint32_t stack_phys = VIRT_TO_PHYS((uintptr_t)stack_pages);
    
    arch_mm_map_user(task, stack_vaddr, stack_phys, 0x7);
    arch_mm_map_user(task, stack_vaddr + PAGE_SIZE, stack_phys + PAGE_SIZE, 0x7);
    
    /* Create stack VMA */
    vma_t *stack_vma = vma_create(task->mm.stack_start, task->mm.stack_end,
                                   VM_READ | VM_WRITE | VM_GROWSDOWN, VMA_STACK);
    if (stack_vma) {
        vma_insert(task, stack_vma);
    }
    
    /* Build the argc/argv/envp block on the new stack and jump to userspace */
    uintptr_t user_esp;
    if (setup_initial_stack(stack_pages, stack_vaddr, &argv, &envp,
                            &user_esp) < 0) {
        printk("[EXEC] Initial stack too small for argv/envp\n");
        return -E2BIG;
    }

    /* Close every fd marked FD_CLOEXEC (POSIX exec semantics). */
    fs_close_on_exec();

    /* Switch to the task's page directory */
    uint32_t cr3 = arch_mm_get_pgd_phys(task);
    printk("[EXEC] pid %d: %s\n", (int)task->pid, task->name);
    
    /* This will never return - it directly jumps to userspace */
    enter_userspace(cr3, img.entry, user_esp);
    
    /* Should never reach here */
    return -1;
}

/* ============================================================================
 * Exec family wrappers
 * ============================================================================ */

int sys_exec(const char *user_path)
{
    return do_exec(user_path, NULL, NULL);
}

int sys_execv(const char *user_path, char **user_argv)
{
    return do_exec(user_path, user_argv, NULL);
}

int sys_execve(const char *user_path, char **user_argv, char **user_envp)
{
    return do_exec(user_path, user_argv, user_envp);
}

/* ============================================================================
 * Working directory
 * ============================================================================ */

int sys_chdir(const char *user_path)
{
    char kernel_path[256];
    
    if (!user_path || !valid_user_pointer(user_path, 1)) {
        printk("[CHDIR] Invalid path pointer\n");
        return -1;
    }
    
    size_t len = 0;
    while (len < sizeof(kernel_path) - 1) {
        if (copy_from_user(&kernel_path[len], &user_path[len], 1) < 0)
            return -1;
        if (kernel_path[len] == '\0')
            break;
        len++;
    }
    kernel_path[sizeof(kernel_path) - 1] = '\0';
    
    /* Require search (execute) permission on the directory. */
    int perm = fs_access_perm(kernel_path, 1);   /* X_OK */
    if (perm < 0)
        return perm;
    
    return fs_chdir(kernel_path);
}

/* ============================================================================
 * Directory reading (getdents)
 * ============================================================================ */

int sys_getdents(int fd, dirent_t *user_buf, unsigned int count)
{
    if (!user_buf || count == 0)
        return 0;
    
    /* Range-check the whole destination buffer */
    if (!valid_user_pointer(user_buf, sizeof(dirent_t) * count))
        return -EFAULT;
    
    unsigned int n = 0;
    for (unsigned int i = 0; i < count; i++) {
        dirent_t de;
        int r = fs_readdir(fd, &de);
        if (r == 0)
            break;               /* end of directory */
        if (r < 0)
            return (n > 0) ? (int)n : -EBADF;
        if (copy_to_user(&user_buf[i], &de, sizeof(dirent_t)) < 0)
            return -EFAULT;
        n++;
    }
    return (int)n;
}

/* ============================================================================
 * Process groups & sessions (POSIX job control foundation)
 * ============================================================================ */

/**
 * sys_setpgid - Set the process group of a process.
 * @param pid  Target PID, or 0 for the calling process
 * @param pgid Desired process group, or 0 to use the target's PID
 * @return 0 on success, -errno on error
 *
 * A process may change its own group or that of an (unexec'd) child.
 */
int sys_setpgid(int pid, int pgid)
{
    if (pgid < 0)
        return -EINVAL;

    task_struct_t *task = current;
    task_struct_t *target = task;

    if (pid != 0 && pid != (int)task->pid) {
        target = find_task_by_pid((uint32_t)pid);
        if (!target)
            return -ESRCH;
        /* Only a parent may set a child's process group (simplified). */
        if (target->ppid != task->pid)
            return -EPERM;
    }

    target->pgid = (pgid == 0) ? target->pid : (uint32_t)pgid;
    return 0;
}

/**
 * sys_getpgid - Get the process group of a process.
 */
int sys_getpgid(int pid)
{
    task_struct_t *task = current;
    if (pid != 0) {
        task = find_task_by_pid((uint32_t)pid);
        if (!task)
            return -ESRCH;
    }
    return (int)task->pgid;
}

/**
 * sys_getpgrp - Get the process group of the calling process.
 */
int sys_getpgrp(void)
{
    return (int)current->pgid;
}

/**
 * sys_setsid - Create a new session.  The caller becomes the session
 * leader and the leader of a new process group (its own PID).
 * @return The new session id, or -errno if already a group leader
 */
int sys_setsid(void)
{
    task_struct_t *task = current;
    if (task->pgid == task->pid)
        return -EPERM;   /* already a process group leader */

    task->sid  = task->pid;
    task->pgid = task->pid;
    return (int)task->sid;
}

/**
 * sys_getsid - Get the session id of a process.
 */
int sys_getsid(int pid)
{
    task_struct_t *task = current;
    if (pid != 0) {
        task = find_task_by_pid((uint32_t)pid);
        if (!task)
            return -ESRCH;
    }
    return (int)task->sid;
}

/* ============================================================================
 * User / group ids
 * ============================================================================ */

int sys_getuid(void)  { return (int)current->uid; }
int sys_geteuid(void) { return (int)current->euid; }
int sys_getgid(void)  { return (int)current->gid; }
int sys_getegid(void) { return (int)current->egid; }

/**
 * sys_setuid - Set real + effective user id.
 * Root may set any uid; a non-root process may only set its own uid/euid.
 */
int sys_setuid(uint32_t uid)
{
    task_struct_t *task = current;
    if (task->euid != 0 && uid != task->uid && uid != task->euid)
        return -EPERM;
    task->uid  = uid;
    task->euid = uid;
    return 0;
}

/**
 * sys_setgid - Set real + effective group id.
 */
int sys_setgid(uint32_t gid)
{
    task_struct_t *task = current;
    if (task->egid != 0 && gid != task->gid && gid != task->egid)
        return -EPERM;
    task->gid  = gid;
    task->egid = gid;
    return 0;
}

/**
 * sys_umask - Set the file creation mask.
 * @param mask New mask (only the permission bits are used)
 * @return The previous mask
 */
int sys_umask(uint32_t mask)
{
    task_struct_t *task = current;
    uint32_t old = task->umask;
    task->umask = mask & 0777;
    return (int)old;
}
