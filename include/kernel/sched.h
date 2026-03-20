#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include "lib/list.h"
#include "fs/fs.h"

/* =========================================================================
 * Process States
 * ========================================================================= */

typedef enum {
    TASK_RUNNING = 0,  /* currently executing */
    TASK_READY,        /* runnable, waiting for CPU */
    TASK_BLOCKED,      /* waiting for an event */
    TASK_ZOMBIE,       /* exited, waiting to be reaped */
} task_state_t;

/* =========================================================================
 * Memory Management Structure (Per-Process)
 * ========================================================================= */

typedef struct mm_struct {
    /* Page directory (embedded, aligned to 4KB) */
    uint32_t pgdir[1024] __attribute__((aligned(4096)));
    
    /* Page tables for user space (allocated on-demand)
     * Indices 0-767 map to 3GB user space (0x00000000 - 0xBFFFFFFF)
     * Indices 768-1023 are kernel space (copied from kernel_page_table) */
    uint32_t *page_tables[768];
    
    /* Memory regions */
    uint32_t code_start;       /* Code segment start (e.g., 0x08000000) */
    uint32_t code_end;         /* Code segment end */
    uint32_t brk_start;        /* Heap start */
    uint32_t brk_end;          /* Current heap end (for sbrk/brk) */
    uint32_t stack_start;      /* User stack bottom */
    uint32_t stack_end;        /* User stack top (e.g., 0xC0000000) */
} mm_struct_t;

/* =========================================================================
 * Task Structure
 * ========================================================================= */

typedef struct task_struct {
    /* ---- Process Identity ---- */
    uint32_t      pid;
    uint32_t      ppid;
    task_state_t  state;
    char          name[32];
    
    /* ---- Scheduler Links ---- */
    list_head_t   task_list;     /* Node in global task list */
    list_head_t   children;      /* Head of children list */
    list_head_t   sibling;       /* Node in parent's children list */
    
    /* ---- CPU Context (saved on context switch) ---- */
    uint32_t      kernel_esp;    /* Kernel stack pointer */
    uint32_t      esp;           /* User stack pointer */
    uint32_t      eip;           /* Instruction pointer */
    uint32_t      eflags;        /* Flags register */
    uint32_t      eax, ebx, ecx, edx;
    uint32_t      esi, edi, ebp;
    
    /* ---- Memory Management (EMBEDDED!) ---- */
    mm_struct_t   mm;            /* Page directory + tables + regions */
    uint32_t      kernel_stack;  /* Kernel stack base (direct-mapped) */
    
    /* ---- File System ---- */
    list_head_t   files;
    int           next_fd;
    char          cwd[MAX_PATH_LEN];
    
    /* ---- Exit Status ---- */
    int           exit_code;
} task_struct_t;

/* =========================================================================
 * User Space Memory Layout
 * ========================================================================= */

#define USER_TEXT_START  0x08000000U  /* User code starts at 128MB */
#define USER_HEAP_START  0x08100000U  /* Heap starts 1MB after code */
#define USER_STACK_TOP   0xC0000000U  /* Stack grows down from kernel base */
#define USER_STACK_SIZE  0x00100000U  /* 1MB stack */

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

/* =========================================================================
 * Context Switching (implemented in context.s)
 * ========================================================================= */

/**
 * Low-level context switch
 * @param old_esp Pointer to save old ESP
 * @param new_esp New ESP to load
 * @param new_cr3 New CR3 (page directory physical address)
 */
void switch_to(uint32_t *old_esp, uint32_t new_esp, uint32_t new_cr3);

#endif /* SCHED_H */