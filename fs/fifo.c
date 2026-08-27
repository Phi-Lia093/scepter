/* ============================================================================
 * Named Pipes (FIFOs)
 *
 * FIFOs are regular inodes on a filesystem (S_IFIFO) but their I/O is
 * carried by an in-memory pipe.  We keep a registry keyed by
 * (fs_id, fs_private, inode) so every open of the same FIFO shares one
 * pipe_t.
 *
 * Open semantics (POSIX):
 *   - open(fifo, O_RDONLY)  blocks until a writer opens (unless O_NONBLOCK)
 *   - open(fifo, O_WRONLY)  blocks until a reader opens (unless O_NONBLOCK,
 *                           in which case it fails with ENXIO)
 *   - open(fifo, O_RDWR)    never blocks, always succeeds
 *   - O_NONBLOCK on the read side succeeds immediately even with no writer
 *
 * The pipe is freed (and the registry entry dropped) when both the last
 * reader and the last writer close.  All calls run in syscall context
 * (IF=0), so check -> sleep_on is race free.
 * ============================================================================ */

#include "fs/fs.h"
#include "fs/pipe.h"
#include "kernel/sched.h"
#include "kernel/syscall.h"
#include "mm/slab.h"
#include "errno.h"

typedef struct fifo_entry {
    list_head_t   node;
    int           fs_id;
    void         *fs_private;
    uint32_t      inode;       /* inode number of the FIFO on disk */
    pipe_t       *pipe;
} fifo_entry_t;

static LIST_HEAD(fifo_list);

void fifo_init(void)
{
    INIT_LIST_HEAD(&fifo_list);
}

/* Remove the registry entry when its pipe is destroyed (both ends closed). */
static void fifo_pipe_destroyed(pipe_t *p)
{
    fifo_entry_t *e, *tmp;
    list_for_each_entry_safe(e, tmp, &fifo_list, node) {
        if (e->pipe == p) {
            list_del(&e->node);
            kfree(e);
            return;
        }
    }
}

static fifo_entry_t *fifo_lookup(int fs_id, void *fs_private, uint32_t inode)
{
    fifo_entry_t *e;
    list_for_each_entry(e, &fifo_list, node) {
        if (e->fs_id == fs_id && e->fs_private == fs_private &&
            e->inode == inode)
            return e;
    }
    return NULL;
}

/* Find (or create) the pipe for a FIFO inode.  Returns NULL on ENOMEM. */
static pipe_t *fifo_get_pipe(int fs_id, void *fs_private, uint32_t inode)
{
    fifo_entry_t *e = fifo_lookup(fs_id, fs_private, inode);
    if (e)
        return e->pipe;

    e = (fifo_entry_t *)kalloc(sizeof(*e));
    if (!e)
        return NULL;

    e->pipe = pipe_create();
    if (!e->pipe) {
        kfree(e);
        return NULL;
    }
    e->fs_id      = fs_id;
    e->fs_private = fs_private;
    e->inode      = inode;
    e->pipe->on_destroy = fifo_pipe_destroyed;
    /* Fresh FIFO starts with no open ends. */
    e->pipe->readers = 0;
    e->pipe->writers = 0;
    list_add_tail(&e->node, &fifo_list);
    return e->pipe;
}

/* Attach the FIFO pipe to a newly opened open_file, applying the POSIX
 * open-side semantics.  Returns 0 on success or a negative errno.
 * On failure the caller must close file_private via the fs driver.
 *
 * The simultaneous-open case (one process opening O_RDONLY while another
 * opens O_WRONLY) must not deadlock: when we block waiting for the other
 * side, we count ourselves as a waiting reader/writer so the other open
 * succeeds immediately and wakes us. */
int fifo_open_end(int fs_id, void *fs_private, uint32_t inode,
                  int flags, open_file_t *file)
{
    pipe_t *p = fifo_get_pipe(fs_id, fs_private, inode);
    if (!p)
        return -ENOMEM;

    int accmode = flags & O_ACCMODE;

    if (accmode == O_RDWR) {
        /* Never blocks (POSIX): counts as both a reader and a writer. */
        p->readers++;
        p->writers++;
        wake_up(&p->open_wq);
    } else if (accmode == O_WRONLY) {
        /* Block until a reader is registered, unless O_NONBLOCK (ENXIO).
         * Note: readers are registered BEFORE the reader's open blocks,
         * so the writer never sees a transient readers==0 here. */
        for (;;) {
            if (p->readers > 0)
                break;
            if (flags & O_NONBLOCK) {
                wake_up(&p->open_wq);
                return -ENXIO;
            }
            sleep_on(&p->open_wq);
            if (current->pending) {
                wake_up(&p->open_wq);
                return -EINTR;
            }
        }
        p->writers++;
        wake_up(&p->open_wq);
    } else { /* O_RDONLY */
        /* Register ourselves as a reader BEFORE waiting, so a concurrent
         * writer-open (or write()) sees readers > 0.  Block until either a
         * writer is open OR data is already buffered (a writer may have
         * written and closed; POSIX FIFO data persists after writers
         * leave). */
        p->readers++;
        wake_up(&p->open_wq);   /* let a blocked writer proceed */
        for (;;) {
            if (p->writers > 0 || p->count > 0)
                break;
            if (flags & O_NONBLOCK)
                break;          /* O_NONBLOCK read-open succeeds now */
            sleep_on(&p->open_wq);
            /* Re-check the success condition first: a writer may have
             * written data and exited, delivering SIGCHLD that woke us.
             * Only treat a pending signal as EINTR if we are STILL blocked
             * (no writer, no data). */
            if (p->writers > 0 || p->count > 0)
                break;
            if (current->pending) {
                p->readers--;
                wake_up(&p->open_wq);
                return -EINTR;
            }
        }
        wake_up(&p->open_wq);
    }

    file->pipe    = p;
    file->is_fifo = 1;
    return 0;
}
