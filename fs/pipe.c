#include "fs/pipe.h"
#include "mm/slab.h"
#include "lib/printk.h"

/* =========================================================================
 * Pipe - anonymous kernel pipe (ring buffer with blocking I/O)
 *
 * All pipe_read/pipe_write calls happen in syscall context with IF=0, so
 * the condition-check -> sleep_on sequence has no missed-wakeup race
 * (wake_up is safe from any context on this single-CPU kernel).
 * ========================================================================= */

pipe_t *pipe_create(void)
{
    pipe_t *p = (pipe_t *)kalloc(sizeof(pipe_t));
    if (!p)
        return NULL;

    p->head    = 0;
    p->tail    = 0;
    p->count   = 0;
    p->readers = 1;
    p->writers = 1;
    init_waitqueue_head(&p->read_wq);
    init_waitqueue_head(&p->write_wq);
    return p;
}

/* Read up to count bytes. Blocks while empty & writers open.
 * Returns bytes read; 0 = EOF (all writers closed); -1 on error. */
int pipe_read(pipe_t *p, char *buf, size_t count)
{
    size_t total = 0;

    if (!p || !buf)
        return -1;

    while (total < count) {
        if (p->count > 0) {
            size_t take = p->count;
            if (take > count - total)
                take = count - total;

            for (size_t i = 0; i < take; i++)
                buf[total + i] = p->buf[(p->tail + i) % PIPE_SIZE];

            p->tail  = (p->tail + take) % PIPE_SIZE;
            p->count -= take;
            total    += take;

            wake_up(&p->write_wq);   /* space freed: wake blocked writers */
        } else if (p->writers == 0) {
            return (int)total;       /* EOF (0 if nothing read yet) */
        } else {
            sleep_on(&p->read_wq);   /* empty: block until data arrives */
        }
    }
    return (int)total;
}

/* Write up to count bytes. Blocks while full & readers open.
 * Returns bytes written; -1 (EPIPE) if all readers closed. */
int pipe_write(pipe_t *p, const char *buf, size_t count)
{
    size_t total = 0;

    if (!p || !buf)
        return -1;

    while (total < count) {
        if (p->readers == 0)
            return -1;               /* EPIPE: nobody will read this */

        if (p->count < PIPE_SIZE) {
            size_t space = PIPE_SIZE - p->count;
            size_t put   = count - total;
            if (put > space)
                put = space;

            for (size_t i = 0; i < put; i++)
                p->buf[(p->head + i) % PIPE_SIZE] = buf[total + i];

            p->head  = (p->head + put) % PIPE_SIZE;
            p->count += put;
            total    += put;

            wake_up(&p->read_wq);    /* data available: wake blocked readers */
        } else {
            sleep_on(&p->write_wq);  /* full: block until space frees */
        }
    }
    return (int)total;
}

/* One read end closed. Wake writers (they see EPIPE). */
void pipe_read_release(pipe_t *p)
{
    if (!p)
        return;
    if (p->readers > 0)
        p->readers--;
    if (p->readers == 0)
        wake_up(&p->write_wq);
    if (p->readers == 0 && p->writers == 0)
        kfree(p);
}

/* One write end closed. Wake readers (they see EOF). */
void pipe_write_release(pipe_t *p)
{
    if (!p)
        return;
    if (p->writers > 0)
        p->writers--;
    if (p->writers == 0)
        wake_up(&p->read_wq);
    if (p->readers == 0 && p->writers == 0)
        kfree(p);
}
