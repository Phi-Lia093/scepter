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
    
    /* Initialize lists */
    INIT_LIST_HEAD(&task->task_list);
    INIT_LIST_HEAD(&task->children);
    INIT_LIST_HEAD(&task->sibling);
    INIT_LIST_HEAD(&task->files);
    
    /* Assign PID */
    task->pid = next_pid++;
    task->state = TASK_READY;
    task->next_fd = 3;
    
    /* Allocate kernel stack (8KB, from direct-mapped region) */
    task->kernel_stack = (uint32_t)page_alloc(8192);
    if (!task->kernel_stack) {
        printk("[SCHED] Failed to allocate kernel stack\n");
        kfree(task);
        return NULL;
    }
    
    /* Initialize kernel_esp to top of stack */
    task->kernel_esp = task->kernel_stack + 8192;
    
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
    
    printk("[SCHED] Freeing task PID %u\n", task->pid);
    
    /* Free kernel stack */
    if (task->kernel_stack) {
        page_free((void *)task->kernel_stack);
    }
    
    /* Free user page tables */
    for (int i = 0; i < 768; i++) {
        if (task->mm.page_tables[i]) {
            page_free(task->mm.page_tables[i]);
        }
    }
    
    /* TODO: Free user pages (walk page tables and free physical pages) */
    
    /* Free task structure */
    kfree(task);
}

/* ============================================================================
 * Task List Management
 * ============================================================================ */

void add_task(task_struct_t *task)
{
    if (!task) return;
    
    list_add_tail(&task->task_list, &task_list);
}

void remove_task(task_struct_t *task)
{
    if (!task || task == &kernel_task) return;
    
    list_del(&task->task_list);
    printk("[SCHED] Removed task PID %u (%s) from scheduler\n", task->pid, task->name);
}

/* ============================================================================
 * Scheduler Core
 * ============================================================================ */

task_struct_t *pick_next_task(void)
{
    task_struct_t *next = NULL;
    list_head_t *pos;
    
    /* Find next READY or RUNNING task after current */
    int found_current = 0;
    list_for_each(pos, &task_list) {
        task_struct_t *task = list_entry(pos, task_struct_t, task_list);
        
        /* Skip ZOMBIE and BLOCKED tasks */
        if (task->state == TASK_ZOMBIE || task->state == TASK_BLOCKED) {
            continue;
        }
        
        if (found_current && task->state == TASK_READY) {
            next = task;
            break;
        }
        
        if (task == current) {
            found_current = 1;
        }
    }
    
    /* Wrap around */
    if (!next) {
        list_for_each(pos, &task_list) {
            task_struct_t *task = list_entry(pos, task_struct_t, task_list);
            
            /* Skip ZOMBIE and BLOCKED tasks */
            if (task->state == TASK_ZOMBIE || task->state == TASK_BLOCKED) {
                continue;
            }
            
            if (task->state == TASK_READY && task != current) {
                next = task;
                break;
            }
        }
    }
    
    /* Keep current if READY and no other task */
    if (!next && current->state == TASK_READY) {
        next = current;
    }
    
    /* Fallback to kernel task */
    if (!next) {
        next = &kernel_task;
    }
    
    return next;
}

void schedule(void)
{
    task_struct_t *prev = current;
    task_struct_t *next = pick_next_task();
    
    // if (next != prev) {
    //     printk("[SCHED] Switching: PID %u -> PID %u\n", prev->pid, next->pid);
    // }
    
    if (next == prev) {
        return;
    }
    
    /* Update states:
     * When leaving kernel task for a user task, block kernel so it's not
     * rescheduled - the idle loop has no useful work once init is running */
    if (prev->state == TASK_RUNNING) {
        if (prev->pid == 0 && next->pid != 0) {
            prev->state = TASK_BLOCKED;  /* Kernel task: don't reschedule */
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
        tss.esp0 = next->kernel_stack + 8192;
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
    
    kernel_task.next_fd = 3;
    kernel_task.cwd[0] = '/';
    kernel_task.cwd[1] = '\0';
    
    /* Kernel task doesn't need user memory management */
    /* Its CR3 is already kernel_page_table */
    
    /* Add to task list */
    list_add(&kernel_task.task_list, &task_list);
    
    printk("[SCHED] Scheduler initialized (kernel task PID 0)\n");
    printk("[SCHED] Kernel files list: %p (next=%p, prev=%p)\n",
           &kernel_task.files, kernel_task.files.next, kernel_task.files.prev);
    printk("[SCHED] current=%p, current->files=%p\n", current, &current->files);
}