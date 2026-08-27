/* ============================================================================
 * Process Scheduler with Embedded Memory Management
 * ============================================================================ */

#include "kernel/sched.h"
#include "kernel/cpu.h"
#include "mm/vma.h"
#include "lib/string.h"
#include "lib/printk.h"
#include "mm/slab.h"
#include "mm/buddy.h"
#include "mm/mm.h"

/* ============================================================================
 * External Symbols
 * ============================================================================ */

/* boot_page_directory: the actual kernel page directory (virtual address) */
extern uint32_t boot_page_directory[];

/* kernel_page_table: 4-byte variable holding the PHYSICAL address of
 * boot_page_directory (set in boot.s via:
 *   mov $boot_page_directory-0xC0000000, kernel_page_table) */
extern uint32_t kernel_page_table;

/* ============================================================================
 * Global Variables
 * ============================================================================ */

/* Static kernel task (PID 0) - no user memory management needed */
static task_struct_t kernel_task;

/* Global task list */
static LIST_HEAD(task_list);

/* Current running task */
task_struct_t *current = &kernel_task;

/* Next PID to assign */
static uint32_t next_pid = 1;

/* ============================================================================
 * Memory Management Initialization
 * ============================================================================ */

/**
 * Initialize memory management for a task
 * Sets up page directory with kernel space copied from kernel_page_table
 */
void init_task_mm(task_struct_t *task)
{
    if (!task) return;
    
    /* Clear entire mm structure */
    memset(&task->mm, 0, sizeof(mm_struct_t));
    
    /* Clear user space page directory entries (0-767) */
    memset(&task->mm.pgdir[0], 0, 768 * sizeof(uint32_t));
    
    /* Copy kernel space page directory entries (768-1023) from actual page directory */
    memcpy(&task->mm.pgdir[768], &boot_page_directory[768], 256 * sizeof(uint32_t));
    
    /* Initialize page table pointers (all NULL initially) */
    memset(task->mm.page_tables, 0, sizeof(task->mm.page_tables));
    
    /* Set up memory region defaults */
    task->mm.code_start = USER_TEXT_START;
    task->mm.code_end = USER_TEXT_START;
    task->mm.brk_start = USER_HEAP_START;
    task->mm.brk_end = USER_HEAP_START;
    task->mm.stack_start = USER_STACK_TOP - USER_STACK_SIZE;
    task->mm.stack_end = USER_STACK_TOP;
    
    /* Initialize VMA list */
    INIT_LIST_HEAD(&task->mm.vma_list);
    task->mm.mmap_base = 0x40000000U;  /* 1GB */
    task->mm.mmap_end = 0xBF000000U;   /* 3GB - 16MB */
}

/* ============================================================================
 * Task Allocation
 * ============================================================================ */

/**
 * Allocate a new task structure (from direct-mapped region)
 */
task_struct_t *alloc_task(void)
{
    /* Allocate from direct-mapped kernel region */
    task_struct_t *task = (task_struct_t *)kalloc(sizeof(task_struct_t));
    if (!task) {
        printk("[SCHED] Failed to allocate task_struct\n");
        return NULL;
    }
    
    /* Zero the entire structure */
    memset(task, 0, sizeof(task_struct_t));

    /* Default resource limits for every task (including PID 0, which runs
     * kernel-internal fs_open during spawn). */
    for (int i = 0; i < RLIM_NLIMITS; i++) {
        task->rlimit_cur[i] = RLIM_INFINITY;
        task->rlimit_max[i] = RLIM_INFINITY;
    }
    task->rlimit_cur[RLIMIT_STACK]  = RLIM_DEFAULT_STACK;
    task->rlimit_max[RLIMIT_STACK]  = RLIM_DEFAULT_STACK;
    task->rlimit_cur[RLIMIT_AS]     = RLIM_DEFAULT_AS;
    task->rlimit_max[RLIMIT_AS]     = RLIM_DEFAULT_AS;
    task->rlimit_cur[RLIMIT_NOFILE] = RLIM_DEFAULT_NOFILE;
    task->rlimit_max[RLIMIT_NOFILE] = RLIM_DEFAULT_NOFILE;
    task->rlimit_cur[RLIMIT_NPROC]  = RLIM_DEFAULT_NPROC;
    task->rlimit_max[RLIMIT_NPROC]  = RLIM_DEFAULT_NPROC;
    task->rlimit_cur[RLIMIT_MEMLOCK] = 0;
    task->rlimit_max[RLIMIT_MEMLOCK] = 0;
    
    /* Initialize lists */
    INIT_LIST_HEAD(&task->task_list);
    INIT_LIST_HEAD(&task->children);
    INIT_LIST_HEAD(&task->sibling);
    INIT_LIST_HEAD(&task->files);
    init_waitqueue_head(&task->wait);
    
    /* Assign PID */
    task->pid = next_pid++;
    task->state = TASK_READY;
    task->next_fd = 0;
    
    /* Allocate kernel stack (16KB = 4 pages, from direct-mapped region) */
    task->kernel_stack = (uint32_t)page_alloc(KERNEL_STACK_SIZE);
    if (!task->kernel_stack) {
        printk("[SCHED] Failed to allocate kernel stack\n");
        kfree(task);
        return NULL;
    }
    
    /* Initialize kernel_esp to top of stack */
    task->kernel_esp = task->kernel_stack + KERNEL_STACK_SIZE;
    
    /* Initialize memory management */
    init_task_mm(task);
    
    return task;
}

/**
 * Free a task structure
 */
void free_task(task_struct_t *task)
{
    if (!task || task == &kernel_task) {
        return;
    }
    
    /* Free kernel stack (16KB = 4 pages, page_free frees one page per call) */
    if (task->kernel_stack) {
        for (int i = 0; i < KERNEL_STACK_PAGES; i++)
            page_free((void *)(task->kernel_stack + i * PAGE_SIZE));
    }
    
    /* Free user data pages mapped in each user page table.
     * (sys_exec keeps task->mm.page_tables in sync with the active CR3,
     * so this walks the process's real, current mappings.) */
    for (int i = 0; i < 768; i++) {
        uint32_t *pt = task->mm.page_tables[i];
        if (!pt) continue;
        for (int j = 0; j < 1024; j++) {
            uint32_t pte = pt[j];
            if (pte & 0x1) {  /* Present */
                page_free((void *)PHYS_TO_VIRT(pte & ~0xFFF));
            }
        }
    }
    
    /* Free user page tables */
    for (int i = 0; i < 768; i++) {
        if (task->mm.page_tables[i]) {
            page_free(task->mm.page_tables[i]);
        }
    }
    
    /* Free task structure itself.
     * task_struct is kalloc'd directly from the buddy allocator (its size
     * is > 2048 so kalloc skips the slab) and spans multiple pages;
     * page_free/kfree would only release the first one. */
    {
        uint32_t pages = (sizeof(task_struct_t) + PAGE_SIZE - 1) >> PAGE_SHIFT;
        for (uint32_t i = 0; i < pages; i++) {
            page_free((void *)((uint32_t)task + i * PAGE_SIZE));
        }
    }
}

/* ============================================================================
 * Task List Management
 * ============================================================================ */

void add_task(task_struct_t *task)
{
    if (!task) return;
    
    list_add_tail(&task->task_list, &task_list);
}

/* Accessor so other kernel subsystems (e.g. signal.c) can iterate the
 * global task list without reaching into this file's static state. */
list_head_t *task_list_head(void)
{
    return &task_list;
}

/* ============================================================================
 * timer_tick - Per-tick housekeeping, called from the PIT interrupt.
 *
 *   - decrements ITIMER_REAL counters (all tasks), firing SIGALRM on expiry
 *   - decrements the current task's ITIMER_VIRTUAL (only when the tick hit
 *     user mode) and ITIMER_PROF (any CPU tick), firing SIGVTALRM/SIGPROF
 *
 * Safe from interrupt context: single-CPU, interrupts disabled while the
 * task list is walked (schedule() also runs inside pit_isr).
 * ============================================================================ */
void timer_tick(int in_user)
{
    list_head_t *pos;

    /* ITIMER_REAL: counts for every task, every tick. */
    list_for_each(pos, &task_list) {
        task_struct_t *t = list_entry(pos, task_struct_t, task_list);

        if (t->itimer_remaining > 0) {
            t->itimer_remaining--;
            if (t->itimer_remaining == 0) {
                extern int send_signal(uint32_t pid, int signum);
                send_signal(t->pid, SIGALRM);
                if (t->itimer_interval > 0)
                    t->itimer_remaining = t->itimer_interval;
            }
        }
    }

    /* ITIMER_VIRTUAL / ITIMER_PROF: only the running task accumulates
     * CPU time, and only while it is actually executing. */
    if (!current || current->pid == 0)
        return;

    if (in_user) {
        if (current->vtimer_remaining > 0) {
            current->vtimer_remaining--;
            if (current->vtimer_remaining == 0) {
                send_signal(current->pid, SIGVTALRM);
                if (current->vtimer_interval > 0)
                    current->vtimer_remaining = current->vtimer_interval;
            }
        }
    }
    if (current->ptimer_remaining > 0) {
        current->ptimer_remaining--;
        if (current->ptimer_remaining == 0) {
            send_signal(current->pid, SIGPROF);
            if (current->ptimer_interval > 0)
                current->ptimer_remaining = current->ptimer_interval;
        }
    }
}

task_struct_t *find_task_by_pid(uint32_t pid)
{
    list_head_t *pos;
    list_for_each(pos, &task_list) {
        task_struct_t *task = list_entry(pos, task_struct_t, task_list);
        if (task->pid == pid) {
            return task;
        }
    }
    return NULL;
}

/* ============================================================================
 * Wait Queues
 * ============================================================================ */

/**
 * sleep_on - Block the current task on a wait queue.
 *
 * The caller is resumed by wake_up() and MUST re-check its condition after
 * returning (wake-ups are level-triggered: every waiter on the queue wakes).
 *
 * Must be called with interrupts disabled: all blocking syscalls run inside
 * the int 0x80 handler with IF=0, which makes the add → block → schedule
 * sequence atomic w.r.t. IRQ-driven wakeups (no missed wakeup race).
 */
void sleep_on(wait_queue_head_t *wq)
{
    wait_queue_t wait;
    wait.task = current;
    wait.active = 1;
    INIT_LIST_HEAD(&wait.node);

    list_add_tail(&wait.node, &wq->task_list);
    current->state = TASK_BLOCKED;
    schedule();

    /* Woken up: wake_up() may already have removed our node. */
    if (wait.active) {
        wait.active = 0;
        list_del(&wait.node);
    }
}

/**
 * wake_up - Wake all tasks sleeping on a wait queue and clear it.
 *
 * Safe to call from interrupt context: IRQ handlers (kbd, pit) run with
 * IF=0 so list manipulation is atomic on this single-CPU kernel.
 */
void wake_up(wait_queue_head_t *wq)
{
    list_head_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &wq->task_list) {
        wait_queue_t *w = list_entry(pos, wait_queue_t, node);
        if (w->task && w->task->state == TASK_BLOCKED) {
            w->task->state = TASK_READY;
        }
        w->active = 0;
        list_del(&w->node);
    }
}

void remove_task(task_struct_t *task)
{
    if (!task || task == &kernel_task) return;
    
    list_del(&task->task_list);
}

/* ============================================================================
 * Scheduler Core
 * ============================================================================ */

task_struct_t *pick_next_task(void)
{
    task_struct_t *next = NULL;
    list_head_t *pos;

    /* Find the best (lowest) nice value among runnable READY tasks that
     * are NOT the current task. */
    int best_other = 1000;
    list_for_each(pos, &task_list) {
        task_struct_t *task = list_entry(pos, task_struct_t, task_list);
        if (task->state != TASK_READY || task == current)
            continue;
        if (task->priority < best_other)
            best_other = task->priority;
    }

    /* If the current task is strictly better than every other runnable
     * task, keep it (priority scheduling: a -20 task is never preempted
     * by a +19 task).  Otherwise round-robin among the best-priority
     * READY tasks (so equal priorities still share the CPU). */
    if (best_other == 1000) {
        /* Nothing else runnable. */
        if (current->state == TASK_RUNNING)
            return current;
    } else if (current->state == TASK_RUNNING &&
               current->priority < best_other) {
        return current;
    }

    /* Round-robin over READY tasks with the best priority. */
    {
        int found_current = 0;
        list_for_each(pos, &task_list) {
            task_struct_t *task = list_entry(pos, task_struct_t, task_list);
            if (task->state != TASK_READY ||
                task->priority != best_other)
                continue;
            if (found_current) {
                next = task;
                break;
            }
            if (task == current)
                found_current = 1;
        }
        if (!next) {   /* wrap around */
            list_for_each(pos, &task_list) {
                task_struct_t *task = list_entry(pos, task_struct_t, task_list);
                if (task->state == TASK_READY &&
                    task->priority == best_other && task != current) {
                    next = task;
                    break;
                }
            }
        }
    }

    /* Keep current if READY and no other task */
    if (!next && current->state == TASK_READY) {
        next = current;
    }

    /* Fallback: the idle (kernel) task.  It is always runnable and runs
     * the sti;hlt loop in kernel_main when nothing else wants the CPU.
     * (It is never picked while any user task is READY, so it does not
     * steal CPU time from runnable processes.) */
    if (!next) {
        next = &kernel_task;
        next->state = TASK_RUNNING;
    }

    return next;
}

void schedule(void)
{
    task_struct_t *prev = current;
    task_struct_t *next = pick_next_task();
    
    if (next == prev) {
        return;
    }
    
    /* Update states.  The idle (kernel) task yields the CPU permanently
     * once user tasks exist; pick_next_task's fallback still returns it
     * when nothing else is runnable, so the kernel idles in sti;hlt. */
    if (prev->state == TASK_RUNNING) {
        if (prev->pid == 0 && next->pid != 0) {
            prev->state = TASK_BLOCKED;
        } else {
            prev->state = TASK_READY;
        }
    }
    next->state = TASK_RUNNING;
    
    /* Update current */
    current = next;
    
    /* Update TSS.esp0 to new task's kernel stack top.
     * This is CRITICAL for ring-3 tasks: when a timer fires while PID N is
     * in ring 3, the CPU uses TSS.esp0 as the ring-0 stack pointer.
     * If esp0 points to the wrong stack, the interrupt frame goes to garbage. */
    if (next->pid != 0) {
        tss.esp0 = next->kernel_stack + KERNEL_STACK_SIZE;
    }
    
    /* Get CR3 (physical address of page directory)
     * kernel_page_table is a uint32_t holding the PHYSICAL addr of boot_page_directory
     * For user tasks, compute from their embedded (aligned) page directory */
    uint32_t new_cr3;
    if (next->pid == 0) {
        /* Kernel task: use stored physical address directly */
        new_cr3 = kernel_page_table;
    } else {
        new_cr3 = VIRT_TO_PHYS((uint32_t)&next->mm.pgdir[0]);
    }
    
    /* Perform context switch */
    switch_to(&prev->kernel_esp, next->kernel_esp, new_cr3);
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

void sched_init(void)
{
    /* Initialize kernel task */
    memset(&kernel_task, 0, sizeof(task_struct_t));
    
    kernel_task.pid = 0;
    kernel_task.ppid = 0;
    kernel_task.state = TASK_RUNNING;
    strncpy(kernel_task.name, "kernel", sizeof(kernel_task.name) - 1);
    
    /* Initialize lists */
    INIT_LIST_HEAD(&kernel_task.task_list);
    INIT_LIST_HEAD(&kernel_task.children);
    INIT_LIST_HEAD(&kernel_task.sibling);
    INIT_LIST_HEAD(&kernel_task.files);
    init_waitqueue_head(&kernel_task.wait);
    
    kernel_task.next_fd = 3;
    kernel_task.cwd[0] = '/';
    kernel_task.cwd[1] = '\0';
    
    /* Default resource limits (memset zeroed them; PID 0 runs kernel-internal
     * fs_open during spawn and must not be rejected by RLIMIT_NOFILE). */
    for (int i = 0; i < RLIM_NLIMITS; i++) {
        kernel_task.rlimit_cur[i] = RLIM_INFINITY;
        kernel_task.rlimit_max[i] = RLIM_INFINITY;
    }
    kernel_task.rlimit_cur[RLIMIT_STACK]  = RLIM_DEFAULT_STACK;
    kernel_task.rlimit_max[RLIMIT_STACK]  = RLIM_DEFAULT_STACK;
    kernel_task.rlimit_cur[RLIMIT_AS]     = RLIM_DEFAULT_AS;
    kernel_task.rlimit_max[RLIMIT_AS]     = RLIM_DEFAULT_AS;
    kernel_task.rlimit_cur[RLIMIT_NOFILE] = RLIM_DEFAULT_NOFILE;
    kernel_task.rlimit_max[RLIMIT_NOFILE] = RLIM_DEFAULT_NOFILE;
    kernel_task.rlimit_cur[RLIMIT_NPROC]  = RLIM_DEFAULT_NPROC;
    kernel_task.rlimit_max[RLIMIT_NPROC]  = RLIM_DEFAULT_NPROC;
    kernel_task.rlimit_cur[RLIMIT_MEMLOCK] = 0;
    kernel_task.rlimit_max[RLIMIT_MEMLOCK] = 0;
    
    /* Kernel task doesn't need user memory management */
    /* Its CR3 is already kernel_page_table */
    
    /* Add to task list */
    list_add(&kernel_task.task_list, &task_list);
    
    printk("[SCHED] Scheduler initialized (kernel task PID 0)\n");
}