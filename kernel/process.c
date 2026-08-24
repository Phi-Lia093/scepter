/* ============================================================================
 * Process Management - exit, fork, exec, wait
 * ============================================================================ */

#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/sched.h"
#include "kernel/cpu.h"
#include "mm/mm.h"
#include "mm/buddy.h"
#include "mm/slab.h"
#include "mm/pgtable.h"
#include "mm/vma.h"
#include "fs/fs.h"
#include "driver/char/pit.h"
#include "lib/printk.h"
#include "lib/string.h"

/* ============================================================================
 * Process Termination (exit)
 * ============================================================================ */

/**
 * sys_exit - Terminate current process
 * @param status Exit status code
 * 
 * This function never returns. The process transitions to ZOMBIE state
 * and waits to be reaped by its parent via wait().
 */
void sys_exit(int status)
{
    task_struct_t *task = current;
    
    /* Store exit code */
    task->exit_code = status;
    
    /* Declare loop variables once for all uses */
    list_head_t *pos, *tmp;
    
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
    extern uint32_t kernel_page_table;
    __asm__ volatile("mov %0, %%cr3" : : "r"(kernel_page_table));
    
    /* Free all VMAs and their pages */
    list_for_each_safe(pos, tmp, &task->mm.vma_list) {
        vma_t *vma = list_entry(pos, vma_t, list);
        
        /* Free all pages in this VMA by directly accessing physical addresses */
        for (uint32_t addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
            uint32_t pdi = addr >> 22;
            uint32_t pti = (addr >> 12) & 0x3FF;
            
            /* Page tables are stored as kernel virtual addresses (direct-mapped) */
            uint32_t *pt = task->mm.page_tables[pdi];
            if (pt) {
                uint32_t pte = pt[pti];
                
                if (pte & 0x1) {  /* Present */
                    uint32_t phys = pte & ~0xFFF;
                    void *virt = (void *)PHYS_TO_VIRT(phys);
                    page_free(virt);
                }
            }
        }
        
        vma_destroy(vma);  /* vma_destroy handles list_del internally */
    }
    
    /* Free page tables (these are already kernel virtual addresses) */
    for (int i = 0; i < 768; i++) {
        if (task->mm.page_tables[i]) {
            page_free(task->mm.page_tables[i]);
            task->mm.page_tables[i] = NULL;
        }
    }
    
    /* Reparent children to init (PID 1) or auto-reap them */
    if (!list_empty(&task->children)) {
        
        list_for_each_safe(pos, tmp, &task->children) {
            task_struct_t *child = list_entry(pos, task_struct_t, sibling);
            
            if (child->state == TASK_ZOMBIE) {
                /* Auto-reap zombie children */
                printk("[PROCESS] Auto-reaping zombie child PID %u\n", child->pid);
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
                    printk("[PROCESS] Reparented PID %u to init (PID %u)\n",
                           child->pid, init_task->pid);
                } else {
                    /* No init yet (or init itself is exiting): orphan it */
                    child->ppid = 1;
                    printk("[PROCESS] PID %u orphaned (no init)\n", child->pid);
                }
            }
        }
    }
    
    /* Transition to ZOMBIE state */
    task->state = TASK_ZOMBIE;
    
    /* Wake up parent if it's waiting in wait() */
    {
        task_struct_t *parent = find_task_by_pid(task->ppid);
        if (parent && parent != task) {
            wake_up(&parent->wait);
            printk("[PROCESS] Woke parent PID %u (child PID %u exited)\n",
                   parent->pid, task->pid);
        }
    }
    
    /* Schedule next task (this never returns) */
    schedule();
    
    /* Should never reach here */
    while(1);
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
        return -1;
    }
    
    /* Copy basic fields */
    child->ppid = parent->pid;
    strncpy(child->name, parent->name, sizeof(child->name));
    strncpy(child->cwd, parent->cwd, sizeof(child->cwd));
    child->next_fd = parent->next_fd;
    
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
        INIT_LIST_HEAD(&cfd->node);
        
        /* Increment refcount on shared open_file */
        if (cfd->file) {
            cfd->file->refcount++;
        }
        
        /* Add to child's file list */
        list_add_tail(&cfd->node, &child->files);
    }
    
    /* Duplicate memory: VMAs and page tables */
    
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
        
        vma_insert(child, cvma);
        
        /* Copy all pages in this VMA */
        int pages_copied = 0;
        for (uint32_t addr = pvma->vm_start; addr < pvma->vm_end; addr += PAGE_SIZE) {
            uint32_t pdi = addr >> 22;
            uint32_t pti = (addr >> 12) & 0x3FF;
            
            /* Check if parent has this page mapped */
            if (!parent->mm.page_tables[pdi]) {
                continue;
            }
            
            uint32_t *parent_pt = parent->mm.page_tables[pdi];
            uint32_t parent_pte = parent_pt[pti];
            
            if (!(parent_pte & 0x1)) {  /* Not present */
                continue;
            }
            
            /* Allocate page table for child if needed */
            if (!child->mm.page_tables[pdi]) {
                uint32_t *pt = (uint32_t *)page_alloc(PAGE_SIZE);
                if (!pt) {
                    printk("[PROCESS] Fork failed: could not allocate page table\n");
                    fork_cleanup_failed(child);
                    return -1;
                }
                memset(pt, 0, PAGE_SIZE);
                child->mm.page_tables[pdi] = pt;
                
                uint32_t pt_phys = VIRT_TO_PHYS((uint32_t)pt);
                child->mm.pgdir[pdi] = pt_phys | 0x7;  /* P | RW | U */
            }
            
            /* Allocate new physical page for child */
            void *child_page = page_alloc(PAGE_SIZE);
            if (!child_page) {
                printk("[PROCESS] Fork failed: could not allocate page\n");
                fork_cleanup_failed(child);
                return -1;
            }
            
            /* Copy page content from parent */
            uint32_t parent_phys = parent_pte & ~0xFFF;
            void *parent_page = (void *)PHYS_TO_VIRT(parent_phys);
            memcpy(child_page, parent_page, PAGE_SIZE);
            
            /* Install PTE in child */
            uint32_t *child_pt = child->mm.page_tables[pdi];
            uint32_t child_phys = VIRT_TO_PHYS((uint32_t)child_page);
            uint32_t flags = parent_pte & 0xFFF;  /* Copy flags */
            child_pt[pti] = child_phys | flags;
            pages_copied++;
        }
        
        (void)pages_copied;  /* Suppress unused variable warning */
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
    
    /* Set up child's kernel stack for first execution
     * When syscall returns, child should get EAX=0 (return value for child)
     * 
     * The parent is currently in syscall context. After fork returns,
     * both parent and child will return to user mode via iret.
     * 
     * We need to set up child's kernel stack to mirror parent's stack
     * so it can also iret back to user mode (to the instruction after int 0x80).
     */
    
    extern void first_entry_trampoline(void);
    
    /* Get parent's current kernel ESP - it has the syscall frame
     * When int 0x80 was invoked, the following was pushed:
     * 1. CPU pushed SS, ESP, EFLAGS, CS, EIP (if from ring 3)
     * 2. isr128 pushed: GS, FS, ES, DS, CR3
     * 3. isr128 pushed: pusha (EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX)
     * 4. isr128 pushed: 6 syscall arguments
     * 
     * We need to find the user's EIP and ESP from the IRET frame */
    
    /* For now, use a simpler approach: parent will continue execution,
     * and child will be set up to return to the same point with EAX=0.
     * 
     * The challenge is we don't have the parent's user EIP/ESP easily accessible.
     * WORKAROUND: Copy parent's entire kernel stack frame and modify EAX */
    
    uint32_t *kstack = (uint32_t *)(child->kernel_esp);
    
    /* Build child's kernel stack for switch_to + first_entry_trampoline
     * Stack grows downward, so we push in REVERSE order:
     * 
     * High address (top of stack)
     *   [IRET frame for first_entry_trampoline to use]
     *   [Return address = first_entry_trampoline]
     *   [EFLAGS for switch_to popfl]
     *   [POPA frame for switch_to]
     * Low address (ESP points here)
     *
     * switch_to will: popa, popfl, ret (to first_entry_trampoline)
     * first_entry_trampoline will: set segments, iret (to user mode)
     */
    
    /* IRET frame (top of stack, high addresses) */
    kstack--; *kstack = regs->ss;          /* SS */
    kstack--; *kstack = regs->user_esp;    /* User ESP */
    kstack--; *kstack = regs->eflags;      /* EFLAGS */
    kstack--; *kstack = regs->cs;          /* CS */
    kstack--; *kstack = regs->eip;         /* EIP */
    
    /* Return address for switch_to's ret instruction */
    kstack--; *kstack = (uint32_t)first_entry_trampoline;
    
    /* EFLAGS for switch_to's popfl (IF=0, will be enabled by iret) */
    kstack--; *kstack = 0x002;
    
    /* POPA frame for switch_to.
     * popa() loads: EDI, ESI, EBP, (skip ESP), EBX, EDX, ECX, EAX.
     * So we must push in REVERSE: EAX first (highest address),
     * EDI last (lowest address = new ESP), exactly like spawn.c does. */
    kstack--; *kstack = 0;                 /* EAX = 0 (child's return value) */
    kstack--; *kstack = regs->ecx;         /* ECX */
    kstack--; *kstack = regs->edx;         /* EDX */
    kstack--; *kstack = regs->ebx;         /* EBX */
    kstack--; *kstack = regs->esp_dummy;   /* dummy ESP (ignored by popa) */
    kstack--; *kstack = regs->ebp;         /* EBP */
    kstack--; *kstack = regs->esi;         /* ESI */
    kstack--; *kstack = regs->edi;         /* EDI (popa reads this first) */
    
    /* Update child's ESP to point to start of POPA frame */
    child->kernel_esp = (uint32_t)kstack;
    
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
    (void)options;
    (void)rusage;
    task_struct_t *parent = current;

    while (1) {
        /* Look for a matching zombie child */
        list_head_t *pos, *tmp;
        int found_live_child = 0;

        list_for_each_safe(pos, tmp, &parent->children) {
            task_struct_t *child = list_entry(pos, task_struct_t, sibling);

            if (pid > 0 && child->pid != (uint32_t)pid)
                continue;   /* not the requested child */

            if (child->state == TASK_ZOMBIE) {
                /* Found a matching zombie! */
                int cpid = child->pid;
                int status = child->exit_code;

                printk("[PROCESS] Wait: PID %u reaping zombie child PID %u (status=%d)\n",
                       parent->pid, cpid, status);

                /* Return status to user if requested */
                if (status_ptr) {
                    if (copy_to_user(status_ptr, &status, sizeof(status)) < 0) {
                        printk("[PROCESS] Wait: bad status pointer 0x%08x\n",
                               (uint32_t)status_ptr);
                    }
                }

                /* Remove from children list */
                list_del(&child->sibling);

                /* Remove from scheduler and free task */
                remove_task(child);
                free_task(child);

                return cpid;
            }

            found_live_child = 1;
        }

        /* No matching zombie. If there is no matching child at all,
         * fail with ECHILD (no such child). */
        if (!found_live_child && pid > 0) {
            printk("[PROCESS] Wait: PID %u has no child %d\n", parent->pid, pid);
            return -1;
        }
        if (!found_live_child && list_empty(&parent->children)) {
            printk("[PROCESS] Wait: PID %u has no children\n", parent->pid);
            return -1;
        }

        /* Matching children exist but none are zombies yet - block. */
        printk("[PROCESS] Wait: PID %u blocking (no zombie children yet)\n",
               parent->pid);

        sleep_on(&parent->wait);  /* Sleep until a child exits */

        /* When we wake up, loop again to check for zombies */
    }
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
        return -1;
    }
    if (copy_from_user(&req, user_req, sizeof(req)) < 0) {
        return -1;
    }
    if (req.tv_sec < 0 || req.tv_nsec < 0 || req.tv_nsec >= 1000000000L) {
        printk("[TIME] nanosleep: invalid time (%d, %d)\n", req.tv_sec, req.tv_nsec);
        return -1;
    }
    
    /* Convert to PIT ticks (100 Hz): 1 tick = 10 ms */
    uint32_t total_ticks = (uint32_t)req.tv_sec * 100UL;
    total_ticks += (uint32_t)((req.tv_nsec + 9999999L) / 10000000L);
    
    uint32_t target = pit_get_ticks() + total_ticks;
    
    printk("[TIME] pid %u nanosleep %u ticks (until tick %u)\n",
           current->pid, total_ticks, target);
    
    /* Sleep until the target tick. The timer IRQ (pit_isr) wakes us each
     * tick; we re-check and go back to sleep until the deadline. */
    while (pit_get_ticks() < target) {
        sleep_on(&timer_wq);
    }
    
    printk("[TIME] pid %u nanosleep done at tick %u\n",
           current->pid, pit_get_ticks());
    
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
int setup_initial_stack(void *stack_pages, uint32_t stack_vaddr,
                        exec_strings_t *argv, exec_strings_t *envp,
                        uint32_t *esp)
{
    uint32_t argc = argv ? argv->count : 0;
    uint32_t envc = envp ? envp->count : 0;
    uint32_t argv_len = argv ? argv->data_len : 0;
    uint32_t envp_len = envp ? envp->data_len : 0;

    uint32_t str_bytes = argv_len + envp_len;
    uint32_t ptr_bytes = 4 * (argc + 1 + envc + 1);

    /* Must fit inside the 8KB (2-page) stack mapping */
    if (str_bytes + ptr_bytes + 16 > 8192)
        return -1;

    uint32_t top = USER_STACK_TOP;              /* 0xC0000000, exclusive */
    uint32_t str_va = top - str_bytes;          /* strings at the top    */
    uint32_t esp0  = (str_va - ptr_bytes) & ~15U; /* pointer block below  */

    char *direct = (char *)stack_pages;

    /* Pack the strings (via the direct map) */
    if (argv_len)
        memcpy(direct + (str_va - stack_vaddr), argv->data, argv_len);
    if (envp_len)
        memcpy(direct + (str_va - stack_vaddr) + argv_len,
               envp->data, envp_len);

    /* Build the pointer block */
    uint32_t *q = (uint32_t *)(direct + (esp0 - stack_vaddr));
    uint32_t idx = 0;
    q[idx++] = argc;
    for (uint32_t i = 0; i < argc; i++)
        q[idx++] = str_va + (uint32_t)(argv->ptrs[i] - argv->data);
    q[idx++] = 0;                    /* argv NULL terminator */
    for (uint32_t i = 0; i < envc; i++)
        q[idx++] = str_va + argv_len +
                   (uint32_t)(envp->ptrs[i] - envp->data);
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
        return -1;
    }
    
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
    
    /* Copy argv/envp into kernel scratch BEFORE the old image is freed
     * (the caller's argv/envp arrays live in that old image). */
    if (copy_exec_strings(user_argv, &argv) < 0) {
        printk("[EXEC] Bad argv pointer\n");
        return -1;
    }
    if (copy_exec_strings(user_envp, &envp) < 0) {
        printk("[EXEC] Bad envp pointer\n");
        return -1;
    }
    
    /* Default argv: {path} when none was supplied */
    if (argv.count == 0) {
        uint32_t plen = strlen(kernel_path) + 1;
        if (plen > EXEC_MAX_DATA)
            return -1;
        strcpy(argv.data, kernel_path);
        argv.ptrs[0] = argv.data;
        argv.count = 1;
        argv.data_len = plen;
    }
    
    /* Open the executable file */
    int fd = fs_open(kernel_path, O_RDONLY);
    if (fd < 0) {
        printk("[EXEC] Failed to open file\n");
        return -1;
    }
    
    /* Get file size */
    int file_size_tmp = fs_seek(fd, 0, SEEK_END);
    if (file_size_tmp <= 0) {
        printk("[EXEC] Invalid file size: %d\n", file_size_tmp);
        fs_close(fd);
        return -1;
    }
    fs_seek(fd, 0, SEEK_SET);
    uint32_t file_size = (uint32_t)file_size_tmp;
    
    /* Switch to kernel page tables before freeing user memory */
    extern uint32_t kernel_page_table;
    __asm__ volatile("mov %0, %%cr3" : : "r"(kernel_page_table));
    
    /* Free all existing user memory (VMAs and pages) */
    list_head_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &task->mm.vma_list) {
        vma_t *vma = list_entry(pos, vma_t, list);
        
        /* Free all pages in this VMA */
        for (uint32_t addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
            uint32_t pdi = addr >> 22;
            uint32_t pti = (addr >> 12) & 0x3FF;
            
            uint32_t *pt = task->mm.page_tables[pdi];
            if (pt) {
                uint32_t pte = pt[pti];
                if (pte & 0x1) {  /* Present */
                    uint32_t phys = pte & ~0xFFF;
                    void *virt = (void *)PHYS_TO_VIRT(phys);
                    page_free(virt);
                }
            }
        }
        
        vma_destroy(vma);
    }
    
    /* Free all page tables */
    for (int i = 0; i < 768; i++) {
        if (task->mm.page_tables[i]) {
            page_free(task->mm.page_tables[i]);
            task->mm.page_tables[i] = NULL;
            task->mm.pgdir[i] = 0;
        }
    }
    
    /* Reset memory regions */
    task->mm.code_start = USER_TEXT_START;
    task->mm.code_end = USER_TEXT_START;
    task->mm.brk_start = USER_HEAP_START;
    task->mm.brk_end = USER_HEAP_START;
    
    /* Load new binary into memory */
    uint32_t num_pages = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t bytes_loaded = 0;
    
    for (uint32_t i = 0; i < num_pages; i++) {
        /* Allocate physical page */
        void *page_virt = page_alloc(PAGE_SIZE);
        if (!page_virt) {
            printk("[EXEC] Failed to allocate page\n");
            fs_close(fd);
            return -1;
        }
        
        /* Read binary data into page */
        uint32_t bytes_to_read = PAGE_SIZE;
        if (bytes_loaded + bytes_to_read > file_size) {
            bytes_to_read = file_size - bytes_loaded;
        }
        
        char *page_buf = (char*)page_virt;
        uint32_t offset = 0;
        while (offset < bytes_to_read) {
            int n = fs_read(fd, page_buf + offset, bytes_to_read - offset);
            if (n <= 0) break;
            offset += n;
        }
        bytes_loaded += offset;
        
        /* Zero rest of page */
        if (offset < PAGE_SIZE) {
            memset(page_buf + offset, 0, PAGE_SIZE - offset);
        }
        
        /* Map into user space */
        uint32_t vaddr = USER_TEXT_START + (i * PAGE_SIZE);
        uint32_t phys = VIRT_TO_PHYS((uint32_t)page_virt);
        
        /* Allocate page table if needed */
        uint32_t pdi = vaddr >> 22;
        uint32_t pti = (vaddr >> 12) & 0x3FF;
        
        if (!task->mm.page_tables[pdi]) {
            uint32_t *pt = (uint32_t *)page_alloc(PAGE_SIZE);
            if (!pt) {
                printk("[EXEC] Failed to allocate page table\n");
                fs_close(fd);
                return -1;
            }
            memset(pt, 0, PAGE_SIZE);
            task->mm.page_tables[pdi] = pt;
            
            uint32_t pt_phys = VIRT_TO_PHYS((uint32_t)pt);
            task->mm.pgdir[pdi] = pt_phys | 0x7;  /* P | RW | U */
        }
        
        /* Install PTE */
        uint32_t *pt = task->mm.page_tables[pdi];
        pt[pti] = phys | 0x7;  /* P | RW | U */
    }
    
    fs_close(fd);
    
    /* Update code region */
    task->mm.code_end = USER_TEXT_START + file_size;
    
    /* Create code VMA */
    vma_t *code_vma = vma_create(USER_TEXT_START, task->mm.code_end,
                                  VM_READ | VM_EXEC, VMA_CODE);
    if (code_vma) {
        vma_insert(task, code_vma);
    }
    
    /* Allocate new user stack (2 pages: [0xBFFFE000, 0xC0000000)).
     * The top page holds the argc/argv/envp block; the lower page gives
     * the new program immediate stack room before demand-paging grows. */
    void *stack_pages = page_alloc(2 * PAGE_SIZE);
    if (!stack_pages) {
        printk("[EXEC] Failed to allocate stack pages\n");
        return -1;
    }
    
    uint32_t stack_vaddr = USER_STACK_TOP - 2 * PAGE_SIZE;
    uint32_t stack_phys = VIRT_TO_PHYS((uint32_t)stack_pages);
    
    uint32_t pdi = stack_vaddr >> 22;
    
    if (!task->mm.page_tables[pdi]) {
        uint32_t *pt = (uint32_t *)page_alloc(PAGE_SIZE);
        if (!pt) {
            printk("[EXEC] Failed to allocate stack page table\n");
            return -1;
        }
        memset(pt, 0, PAGE_SIZE);
        task->mm.page_tables[pdi] = pt;
        
        uint32_t pt_phys = VIRT_TO_PHYS((uint32_t)pt);
        task->mm.pgdir[pdi] = pt_phys | 0x7;
    }
    
    uint32_t *pt = task->mm.page_tables[pdi];
    pt[(stack_vaddr >> 12) & 0x3FF] = stack_phys | 0x7;
    pt[((stack_vaddr + PAGE_SIZE) >> 12) & 0x3FF] = (stack_phys + PAGE_SIZE) | 0x7;
    
    /* Create stack VMA */
    vma_t *stack_vma = vma_create(task->mm.stack_start, task->mm.stack_end,
                                   VM_READ | VM_WRITE | VM_GROWSDOWN, VMA_STACK);
    if (stack_vma) {
        vma_insert(task, stack_vma);
    }
    
    /* Build the argc/argv/envp block on the new stack and jump to userspace */
    uint32_t user_esp;
    if (setup_initial_stack(stack_pages, stack_vaddr, &argv, &envp,
                            &user_esp) < 0) {
        printk("[EXEC] Initial stack too small for argv/envp\n");
        return -1;
    }
    
    printk("[EXEC] pid %u -> %s (argc=%u, envc=%u, user_esp=0x%08x)\n",
           task->pid, kernel_path, argv.count, envp.count, user_esp);
    
    extern void enter_userspace(uint32_t cr3, uint32_t entry, uint32_t user_esp);
    
    /* Switch to the task's page directory */
    uint32_t cr3 = VIRT_TO_PHYS((uint32_t)&task->mm.pgdir[0]);
    
    /* This will never return - it directly jumps to userspace */
    enter_userspace(cr3, USER_TEXT_START, user_esp);
    
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
        return -1;
    
    unsigned int n = 0;
    for (unsigned int i = 0; i < count; i++) {
        dirent_t de;
        int r = fs_readdir(fd, &de);
        if (r == 0)
            break;               /* end of directory */
        if (r < 0)
            return (n > 0) ? (int)n : -1;
        if (copy_to_user(&user_buf[i], &de, sizeof(dirent_t)) < 0)
            return -1;
        n++;
    }
    return (int)n;
}
