#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include "lib/list.h"
#include "fs/fs.h"
#include "kernel/signal.h"
#include "arch/abi.h"

/* =========================================================================
 * Process States
 * ========================================================================= */

typedef enum {
    TASK_RUNNING = 0,  /* currently executing */
    TASK_READY,        /* runnable, waiting for CPU */
    TASK_BLOCKED,      /* waiting for an event */
    TASK_ZOMBIE,       /* exited, waiting to be reaped */
    TASK_STOPPED,      /* stopped by SIGSTOP/SIGTSTP/etc. (SIGCONT resumes) */
} task_state_t;

/* =========================================================================
 * Wait Queue Head (forward declaration of the waiter type is below)
 * ========================================================================= */

/* A wait queue head – a linked list of wait_queue_t nodes.
 * Defined before task_struct so tasks can embed one. */
typedef struct {
    list_head_t task_list;
} wait_queue_head_t;

#define init_waitqueue_head(wq) INIT_LIST_HEAD(&(wq)->task_list)

/* =========================================================================
 * Memory Management Structure (Per-Process)
 * ========================================================================= */

typedef struct mm_struct {
    /* Architecture-specific MMU state (page tables etc.).  Generic code
     * must only access it through the arch_mm_* API in arch/paging.h. */
    arch_mm_t arch;

    /* Memory regions (legacy, for compatibility) */
    uintptr_t code_start;       /* Code segment start (e.g., 0x08000000) */
    uintptr_t code_end;         /* Code segment end */
    uintptr_t brk_start;        /* Heap start */
    uintptr_t brk_end;          /* Current heap end (for sbrk/brk) */
    uintptr_t stack_start;      /* User stack bottom */
    uintptr_t stack_end;        /* User stack top (e.g., 0xC0000000) */
    
    /* VMA management */
    list_head_t vma_list;      /* List of all VMAs */
    uintptr_t mmap_base;        /* Start of mmap region (0x40000000) */
    uintptr_t mmap_end;         /* End of mmap region (0xBF000000) */
} mm_struct_t;

/* =========================================================================
 * Resource limits (Linux RLIMIT_* numbers, 32-bit)
 * ========================================================================= */

#define RLIMIT_CPU      0
#define RLIMIT_FSIZE    1
#define RLIMIT_DATA     2
#define RLIMIT_STACK    3
#define RLIMIT_CORE     4
#define RLIMIT_RSS      5
#define RLIMIT_NPROC    6
#define RLIMIT_NOFILE   7
#define RLIMIT_MEMLOCK  8
#define RLIMIT_AS       9
#define RLIMIT_LOCKS   10
#define RLIM_NLIMITS   11

#define RLIM_INFINITY  0xFFFFFFFFU

/* Default limits applied to every task. */
#define RLIM_DEFAULT_STACK   0x00100000U   /* 1 MB user stack    */
#define RLIM_DEFAULT_AS      0x40000000U   /* 1 GB address space */
#define RLIM_DEFAULT_NOFILE  1024
#define RLIM_DEFAULT_NPROC   64

/* =========================================================================
 * Task Structure
 * ========================================================================= */

typedef struct task_struct {
    /* ---- Process Identity ---- */
    uint32_t      pid;
    uint32_t      ppid;
    uint32_t      pgid;          /* process group id (0 = none yet)  */
    uint32_t      sid;           /* session id (0 = none yet)        */
    uint32_t      uid;           /* real user id                     */
    uint32_t      euid;          /* effective user id                */
    uint32_t      suid;          /* saved user id                    */
    uint32_t      gid;           /* real group id                    */
    uint32_t      egid;          /* effective group id               */
    uint32_t      sgid;          /* saved group id                   */
    uint32_t      fsuid;         /* filesystem user id               */
    uint32_t      fsgid;         /* filesystem group id              */
    uint32_t      ngroups;       /* number of supplementary groups   */
    uint32_t      groups[16];    /* supplementary group list         */
    uint32_t      umask;         /* file creation mask               */
    uint32_t      personality;   /* execution domain (PER_LINUX=0)   */
    uint32_t      rlimit_cur[RLIM_NLIMITS];   /* soft limits         */
    uint32_t      rlimit_max[RLIM_NLIMITS];   /* hard limits         */
    uintptr_t cleartid;      /* set_tid_address pointer (0=none) */
    char          name[32];
    int           priority;        /* nice value; lower = higher priority */
    task_state_t  state;
    
    /* ---- Scheduler Links ---- */
    list_head_t   task_list;     /* Node in global task list */
    list_head_t   children;      /* Head of children list */
    list_head_t   sibling;       /* Node in parent's children list */
    wait_queue_head_t wait;      /* Wait queue (for wait()/blocking) */
    
    /* ---- CPU Context (saved on context switch) ---- */
    uintptr_t kernel_esp;    /* Kernel stack pointer */
    uintptr_t esp;           /* User stack pointer */
    uintptr_t eip;           /* Instruction pointer */
    uintptr_t eflags;        /* Flags register */
    uintptr_t eax, ebx, ecx, edx;
    uintptr_t esi, edi, ebp;
    
    /* ---- Memory Management (EMBEDDED!) ---- */
    mm_struct_t   mm;            /* Page directory + tables + regions */
    uintptr_t kernel_stack;  /* Kernel stack base (direct-mapped) */
    
    /* ---- File System ---- */
    list_head_t   files;
    int           next_fd;
    char          cwd[MAX_PATH_LEN];
    char          root[MAX_PATH_LEN];   /* chroot() root dir (default "/") */
    
    /* ---- Exit Status ---- */
    int           exit_code;

    /* ---- Signals ---- */
    uint32_t      pending;            /* pending signal mask (bit i = signal i) */
    uint32_t      blocked;            /* blocked signal mask                    */
    uint32_t      sig_handlers[NSIG]; /* user handler / SIG_DFL / SIG_IGN       */
    uint32_t      sig_hmask[NSIG];    /* extra signals blocked during handler   */
    uint32_t      sig_hflags[NSIG];   /* SA_* flags for each signal             */
    int           sig_active;         /* 1 while a catchable handler is running */
    int           sig_delivered;      /* the signal being handled               */
    uintptr_t sig_saved_eip;      /* saved user context (handler entry)     */
    uintptr_t sig_saved_esp;
    uintptr_t sig_saved_eflags;
    uint32_t      sig_saved_blocked;  /* blocked mask before handler entry      */

    /* ---- Stop / Continue (job control) ---- */
    uint32_t      stop_sig;        /* signal that stopped us (0 = running)    */
    int           stop_reported;   /* wait4(WUNTRACED) already reported stop  */
    int           continued;       /* SIGCONT delivered while stopped         */

    /* ---- Interval timers ---- */
    /* ITIMER_REAL    -> SIGALRM   (real elapsed time, all ticks)      */
    uint32_t      itimer_remaining; /* ticks until SIGALRM (0 = off)    */
    uint32_t      itimer_interval;  /* reload value in ticks (0 = oneshot) */
    /* ITIMER_VIRTUAL -> SIGVTALRM (CPU time in user mode)             */
    uint32_t      vtimer_remaining;
    uint32_t      vtimer_interval;
    /* ITIMER_PROF    -> SIGPROF   (CPU time, user + system)           */
    uint32_t      ptimer_remaining;
    uint32_t      ptimer_interval;

    /* ---- CPU time accounting (times()/clock_gettime) ---- */
    uint32_t      uticks;          /* ticks in user mode                     */
    uint32_t      sticks;          /* ticks in kernel mode                   */
} task_struct_t;

/* =========================================================================
 * Current Task
 * ========================================================================= */

extern task_struct_t *current;

/* =========================================================================
 * Scheduler Functions
 * ========================================================================= */

/**
 * Initialize the scheduler subsystem
 * Creates the kernel task (PID 0) and sets current to point at it
 */
void sched_init(void);

/**
 * Pick next task to run (round-robin)
 */
task_struct_t *pick_next_task(void);

/**
 * Perform context switch
 */
void schedule(void);

/**
 * Add task to scheduler
 */
void add_task(task_struct_t *task);

/**
 * Remove task from scheduler
 */
void remove_task(task_struct_t *task);

/**
 * Accessor for the global task list head (used by signal.c to iterate).
 */
list_head_t *task_list_head(void);
void timer_tick(int in_user);

/**
 * Allocate a new task structure
 */
task_struct_t *alloc_task(void);

/**
 * Free a task structure
 */
void free_task(task_struct_t *task);

/**
 * Initialize memory management for a task
 * Sets up page directory with kernel mappings
 */
void init_task_mm(task_struct_t *task);

/**
 * Find a task in the global task list by PID
 * @param pid PID to search for
 * @return Pointer to task, or NULL if not found
 */
task_struct_t *find_task_by_pid(uint32_t pid);

/* =========================================================================
 * Wait Queues
 *
 * Simple wait-queue used by blocking operations:
 *   - wait() sleeps on the parent's wait queue, woken by sys_exit
 *   - blocking tty reads sleep on the keyboard device's wait queue
 *   - nanosleep() sleeps on the PIT timer wait queue
 *
 * All blocking happens inside the int 0x80 syscall handler which runs with
 * interrupts disabled (IF=0), so the add→block→schedule sequence in
 * sleep_on() is atomic w.r.t. IRQ-driven wakeups (no missed wakeups).
 * ========================================================================= */

/* One waiter. Embedded on the sleeping task's kernel stack, so it stays
 * valid for the whole sleep. */
typedef struct wait_queue {
    task_struct_t *task;   /* sleeping task                        */
    list_head_t    node;   /* list node in wait_queue_head_t       */
    int            active; /* 1 while linked; wake_up() clears it  */
} wait_queue_t;

/**
 * Block the current task on a wait queue until wake_up().
 * After resuming, the caller MUST re-check its condition (wake-ups are
 * level-triggered: every waiter on the queue is woken).
 * Must be called with interrupts disabled (true for all syscall handlers).
 */
void sleep_on(wait_queue_head_t *wq);

/**
 * Wake all tasks sleeping on a wait queue and clear it.
 * Safe to call from interrupt context (IRQ handlers run with IF=0).
 */
void wake_up(wait_queue_head_t *wq);

/* =========================================================================
 * Context Switching (implemented in context.s)
 * ========================================================================= */

/**
 * Low-level context switch
 * @param old_esp Pointer to save old ESP
 * @param new_esp New ESP to load
 * @param new_cr3 New CR3 (page directory physical address)
 */
void switch_to(uintptr_t *old_esp, uintptr_t new_esp, uintptr_t new_cr3);

#endif /* SCHED_H */