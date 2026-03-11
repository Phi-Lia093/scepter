#include "kernel/sched.h"
#include "lib/string.h"

/* =========================================================================
 * Static kernel task
 *
 * Before a real scheduler exists there is exactly one task: the kernel
 * itself.  It is statically allocated so that it is available from the
 * very beginning of kernel_main(), before any dynamic allocator is ready.
 * ========================================================================= */

static task_struct_t kernel_task;

/* The globally visible "current task" pointer */
task_struct_t *current = &kernel_task;

/* =========================================================================
 * sched_init
 * ========================================================================= */

void sched_init(void)
{
    kernel_task.pid   = 0;
    kernel_task.state = TASK_RUNNING;
    strncpy(kernel_task.name, "kernel", sizeof(kernel_task.name) - 1);
    kernel_task.name[sizeof(kernel_task.name) - 1] = '\0';

    INIT_LIST_HEAD(&kernel_task.files);
    kernel_task.next_fd = 3;   /* 0/1/2 reserved for stdin/stdout/stderr */

    /* Start with root as working directory */
    kernel_task.cwd[0] = '/';
    kernel_task.cwd[1] = '\0';
}