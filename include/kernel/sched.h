#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include "lib/list.h"
#include "fs/fs.h"   /* MAX_PATH_LEN */

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
 * Task Structure
 *
 * Minimal skeleton – only fields needed by the current kernel are present.
 * TODO: add stack pointer, register context, mm_struct, signal mask, …
 * ========================================================================= */

typedef struct task_struct {
    uint32_t      pid;         /* process ID */
    task_state_t  state;       /* current process state */
    char          name[32];    /* human-readable name */

    /* ---- Open file descriptors ---- */
    list_head_t   files;              /* head of file_handle_t.node list  */
    int           next_fd;            /* next FD number to assign (>= 3)  */

    /* ---- Working directory ---- */
    char          cwd[MAX_PATH_LEN];  /* current working directory path   */
} task_struct_t;

/* =========================================================================
 * Current Task
 *
 * Points to the currently running task_struct.
 * With no scheduler this is always the single kernel task; a real scheduler
 * would update this on every context switch.
 * ========================================================================= */

extern task_struct_t *current;

/* =========================================================================
 * Initialisation
 * ========================================================================= */

/**
 * Initialise the scheduler subsystem.
 * Creates the initial kernel task and sets `current` to point at it.
 * Must be called after slab_init() and before vfs_init().
 */
void sched_init(void);

#endif /* SCHED_H */