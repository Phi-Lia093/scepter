/* ============================================================================
 * Advisory File Locks (flock + fcntl record locks)
 *
 * flock():  whole-file advisory locks associated with the open file
 *           description (open_file_t), i.e. dup()'d fds share the lock and
 *           two independent opens conflict.  Locks are released when the
 *           last fd referencing the open_file_t is closed.
 *
 * fcntl(F_SETLK/F_SETLKW/F_GETLK): byte-range record locks owned by a
 *           process.  Implemented as a small per-open_file list; closing any
 *           fd for the file drops that process's locks on it (POSIX).
 *
 * All lock manipulation happens in syscall context with IF=0, so
 * check -> sleep_on is race free (wake_up is safe from any context).
 * ============================================================================ */

#include "fs/fs.h"
#include "fs/pipe.h"
#include "kernel/sched.h"
#include "kernel/syscall.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "errno.h"

/* copy_from_user / copy_to_user are defined in kernel/syscall.c */
extern int copy_from_user(void *kernel_dst, const void *user_src, size_t n);
extern int copy_to_user(void *user_dst, const void *kernel_src, size_t n);

/* flock(2) operation values (Linux ABI) */
#ifndef LOCK_SH
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#endif

/* fcntl record-lock commands (Linux ABI) */
#ifndef F_GETLK
#define F_GETLK  5
#define F_SETLK  6
#define F_SETLKW 7
#define F_RDLCK  0
#define F_WRLCK  1
#define F_UNLCK  2
#endif

/* One record lock (byte range owned by a process, tied to an open_file). */
typedef struct flock_rec {
    list_head_t   node;      /* linked into open_file->locks            */
    int           type;      /* F_RDLCK / F_WRLCK / F_UNLCK             */
    uint32_t      start;     /* byte offset                             */
    uint32_t      len;       /* byte length (0 = to EOF)                */
    uint32_t      owner;     /* pid that owns the lock                  */
    open_file_t  *file;      /* back-pointer to the open_file           */
} flock_rec_t;

/* Global wait queue for blocking flock/fcntl acquisitions. */
static wait_queue_head_t lock_wq;

void locks_init(void)
{
    init_waitqueue_head(&lock_wq);
}

/* --------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

static int find_open_file(int fd, open_file_t **out)
{
    list_head_t *pos;
    list_for_each(pos, &current->files) {
        fd_entry_t *e = list_entry(pos, fd_entry_t, node);
        if (e->fd == fd) {
            *out = e->file;
            return 0;
        }
    }
    return -1;
}

/* Does this open_file refer to the same underlying file as `other`?
 * Two opens of the same path share the fs + inode.  Pipes are excluded
 * (flock on a pipe affects only that pipe, so identity is the pipe ptr). */
static int same_file(open_file_t *a, open_file_t *b)
{
    if (!a || !b) return 0;
    if (a->pipe || b->pipe) return a->pipe == b->pipe;
    if (a->fs_id != b->fs_id) return 0;
    if (a->fs_private != b->fs_private) return 0;
    /* Compare inode via the filesystem's fstat. */
    fs_ops_t *ops = fs_get_ops(a->fs_id);
    if (ops && ops->fstat) {
        stat_t sa, sb;
        if (ops->fstat(a->file_private, &sa) != 0)
            return 0;
        if (fs_get_ops(b->fs_id) && fs_get_ops(b->fs_id)->fstat) {
            if (fs_get_ops(b->fs_id)->fstat(b->file_private, &sb) != 0)
                return 0;
            return sa.inode == sb.inode;
        }
    }
    /* Fall back: same file_private pointer means same open. */
    return a->file_private == b->file_private;
}

/* --------------------------------------------------------------------------
 * flock()
 * ------------------------------------------------------------------------ */

/* Does any other task's open_file hold a lock on the same file that would
 * conflict with `type`?  flock conflicts are whole-file. */
static int flock_conflict(open_file_t *me, int type, uint32_t *blocker)
{
    list_head_t *head = task_list_head();
    list_head_t *pos;

    list_for_each(pos, head) {
        task_struct_t *t = list_entry(pos, task_struct_t, task_list);
        list_head_t *fp;
        list_for_each(fp, &t->files) {
            fd_entry_t *e = list_entry(fp, fd_entry_t, node);
            open_file_t *of = e->file;
            if (!of || of == me) continue;
            if (of->flock_type == 0) continue;
            if (!same_file(me, of)) continue;
            /* LOCK_EX conflicts with any lock; LOCK_SH conflicts with EX. */
            if (type == LOCK_EX || of->flock_type == LOCK_EX) {
                if (blocker) *blocker = of->flock_owner;
                return 1;
            }
        }
    }
    return 0;
}

int fs_flock(int fd, int op)
{
    open_file_t *file;
    if (find_open_file(fd, &file) != 0)
        return -EBADF;

    int type = op & (LOCK_SH | LOCK_EX);

    if (op & LOCK_UN) {
        file->flock_type  = 0;
        file->flock_owner = 0;
        wake_up(&lock_wq);
        return 0;
    }

    if (type != LOCK_SH && type != LOCK_EX)
        return -EINVAL;

    /* Already hold a compatible lock on this open_file? */
    if (file->flock_type == type) {
        file->flock_owner = current->pid;
        return 0;
    }

    /* Blocking acquisition loop (IF=0: no missed wakeup). */
    for (;;) {
        uint32_t blocker = 0;
        if (!flock_conflict(file, type, &blocker)) {
            /* If we held the other kind, upgrade/downgrade. */
            file->flock_type  = type;
            file->flock_owner = current->pid;
            return 0;
        }
        if (op & LOCK_NB)
            return -EAGAIN;
        sleep_on(&lock_wq);
        if (current->pending)
            return -EINTR;
    }
}

/* --------------------------------------------------------------------------
 * fcntl() record locks
 * ------------------------------------------------------------------------ */

/* Do two byte ranges overlap?  len == 0 means "to EOF" (infinite). */
static int ranges_overlap(uint32_t a_start, uint32_t a_len,
                          uint32_t b_start, uint32_t b_len)
{
    uint32_t a_end = a_len ? a_start + a_len : 0xFFFFFFFFu;
    uint32_t b_end = b_len ? b_start + b_len : 0xFFFFFFFFu;
    if (a_start >= b_end) return 0;
    if (b_start >= a_end) return 0;
    return 1;
}

/* Is there a record lock on `file` (held by a *different* process) that
 * would block a lock of `type` over [start, len]?  Writes conflict with
 * anything; reads only conflict with writes. */
static flock_rec_t *rec_conflict(open_file_t *file, int type,
                                 uint32_t start, uint32_t len,
                                 uint32_t owner, int *conflicting_owner)
{
    list_head_t *head = task_list_head();
    list_head_t *pos;

    list_for_each(pos, head) {
        task_struct_t *t = list_entry(pos, task_struct_t, task_list);
        list_head_t *fp;
        list_for_each(fp, &t->files) {
            fd_entry_t *e = list_entry(fp, fd_entry_t, node);
            open_file_t *of = e->file;
            if (!of || of == file) continue;
            if (!same_file(file, of)) continue;

            flock_rec_t *rec;
            list_for_each_entry(rec, &of->locks, node) {
                if (rec->type == F_UNLCK) continue;
                if (rec->owner == owner) continue;  /* own locks never conflict */
                if (!ranges_overlap(start, len, rec->start, rec->len))
                    continue;
                if (type == F_WRLCK || rec->type == F_WRLCK) {
                    if (conflicting_owner) *conflicting_owner = (int)rec->owner;
                    return rec;
                }
            }
        }
    }
    return NULL;
}

/* Remove all record locks on `file` owned by `owner`. */
static void rec_remove_owner(open_file_t *file, uint32_t owner)
{
    flock_rec_t *rec, *tmp;
    list_for_each_entry_safe(rec, tmp, &file->locks, node) {
        if (rec->owner == owner) {
            list_del(&rec->node);
            kfree(rec);
        }
    }
}

/* Drop every record lock and flock lock held on `file` when the last
 * reference to the open_file is closed (POSIX / flock semantics). */
void locks_release_file(open_file_t *file)
{
    if (!file) return;
    if (!list_empty(&file->locks)) {
        flock_rec_t *rec, *tmp;
        list_for_each_entry_safe(rec, tmp, &file->locks, node) {
            list_del(&rec->node);
            kfree(rec);
        }
        wake_up(&lock_wq);
    }
    if (file->flock_type) {
        file->flock_type  = 0;
        file->flock_owner = 0;
        wake_up(&lock_wq);
    }
}

int fs_fcntl_lock(int fd, int cmd, struct flock_k *user_flk)
{
    open_file_t *file;
    if (find_open_file(fd, &file) != 0)
        return -EBADF;

    struct flock_k flk;
    if (copy_from_user(&flk, user_flk, sizeof(flk)) < 0)
        return -EFAULT;

    /* Normalise l_whence/l_start into an absolute start offset. */
    uint32_t start = flk.l_start;
    if (flk.l_whence == 1) {            /* SEEK_CUR */
        start += file->offset;
    } else if (flk.l_whence == 2) {     /* SEEK_END */
        stat_t st;
        fs_ops_t *ops = fs_get_ops(file->fs_id);
        if (ops && ops->fstat)
            ops->fstat(file->file_private, &st);
        else
            st.size = 0;
        start += st.size;
    }
    uint32_t len = (flk.l_len == 0) ? 0 : (uint32_t)flk.l_len;

    switch (cmd) {
        case F_SETLK:
        case F_SETLKW: {
            if (flk.l_type == F_UNLCK) {
                rec_remove_owner(file, current->pid);
                wake_up(&lock_wq);
                return 0;
            }
            if (flk.l_type != F_RDLCK && flk.l_type != F_WRLCK)
                return -EINVAL;

            for (;;) {
                int blocker = 0;
                if (!rec_conflict(file, flk.l_type, start, len,
                                  current->pid, &blocker)) {
                    /* Replace this process's locks on the file with the
                     * new request (simplified POSIX: one lock per owner). */
                    rec_remove_owner(file, current->pid);
                    flock_rec_t *rec = (flock_rec_t *)kalloc(sizeof(*rec));
                    if (!rec)
                        return -ENOMEM;
                    rec->type  = flk.l_type;
                    rec->start = start;
                    rec->len   = len;
                    rec->owner = current->pid;
                    rec->file  = file;
                    list_add_tail(&rec->node, &file->locks);
                    return 0;
                }
                if (cmd == F_SETLK)
                    return -EAGAIN;
                sleep_on(&lock_wq);
                if (current->pending)
                    return -EINTR;
            }
        }
        case F_GETLK: {
            int blocker = 0;
            flock_rec_t *rec = rec_conflict(file, F_WRLCK, start, len,
                                            current->pid, &blocker);
            struct flock_k out;
            if (rec) {
                out.l_type  = (short)rec->type;
                out.l_whence = 0;
                out.l_start = (int)rec->start;
                out.l_len   = (int)rec->len;
                out.l_pid   = (int)rec->owner;
            } else {
                out.l_type  = F_UNLCK;
                out.l_whence = 0;
                out.l_start = (int)start;
                out.l_len   = (int)len;
                out.l_pid   = 0;
            }
            if (copy_to_user(user_flk, &out, sizeof(out)) < 0)
                return -EFAULT;
            return 0;
        }
        default:
            return -EINVAL;
    }
}

