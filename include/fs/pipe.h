#ifndef FS_PIPE_H
#define FS_PIPE_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/sched.h"   /* wait_queue_head_t */

#define PIPE_SIZE 4096

/*
 * An anonymous pipe: a fixed-size ring buffer shared between a read end
 * and a write end (each is its own open_file_t pointing at this struct).
 *
 * Blocking semantics:
 *   - read() blocks while the buffer is empty AND writers remain open;
 *     returns 0 (EOF) once all write ends are closed.
 *   - write() blocks while the buffer is full AND readers remain open;
 *     returns -1 (EPIPE) once all read ends are closed.
 *   - Both ends closing frees the pipe.
 */
typedef struct pipe {
    char             buf[PIPE_SIZE];
    uint32_t         head;      /* next write position      */
    uint32_t         tail;      /* next read position       */
    uint32_t         count;     /* bytes currently buffered */
    uint32_t         readers;   /* number of open read ends */
    uint32_t         writers;   /* number of open write ends */
    wait_queue_head_t read_wq;  /* readers sleeping here    */
    wait_queue_head_t write_wq; /* writers sleeping here    */
} pipe_t;

pipe_t *pipe_create(void);
int    pipe_read(pipe_t *p, char *buf, size_t count);
int    pipe_write(pipe_t *p, const char *buf, size_t count);
void   pipe_read_release(pipe_t *p);   /* close one read end  */
void   pipe_write_release(pipe_t *p);  /* close one write end */

#endif /* FS_PIPE_H */
