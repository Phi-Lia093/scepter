/* ============================================================================
 * procfs - process/statistics filesystem
 *
 * A read-only pseudo filesystem mounted at /proc.  Every entry is generated
 * on open() into a freshly allocated buffer, so the content always reflects
 * the system state at the time the file is opened (Linux-style procfs
 * semantics).
 *
 * Files:
 *   /proc/version   - kernel identity
 *   /proc/uptime    - seconds since boot
 *   /proc/meminfo   - memory totals (buddy allocator)
 *   /proc/cpuinfo   - CPU description
 *   /proc/loadavg   - load average (always 0: single CPU, no load tracking)
 *   /proc/stat      - process counts
 *   /proc/tasks     - one line per task (pid ppid state name prio uticks stics)
 * ============================================================================ */

#include "fs/procfs.h"
#include "fs/fs.h"
#include "kernel/sched.h"
#include "kernel/syscall.h"
#include "mm/buddy.h"
#include "mm/slab.h"
#include "lib/string.h"
#include "lib/printk.h"
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Growable string buffer used to build proc file contents
 * ============================================================================ */

#define PROC_BUF_INIT 512

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
    int     oom;
} procbuf_t;

static void pb_reserve(procbuf_t *pb, size_t extra)
{
    if (pb->oom) return;
    if (pb->len + extra + 1 <= pb->cap) return;

    size_t ncap = pb->cap ? pb->cap : PROC_BUF_INIT;
    while (ncap < pb->len + extra + 1)
        ncap *= 2;

    char *nd = (char *)kalloc(ncap);
    if (!nd) {
        pb->oom = 1;
        return;
    }
    if (pb->data && pb->len) {
        memcpy(nd, pb->data, pb->len);
        kfree(pb->data);
    }
    pb->data = nd;
    pb->cap  = ncap;
}

static void pb_putc(procbuf_t *pb, char c)
{
    pb_reserve(pb, 1);
    if (pb->oom) return;
    pb->data[pb->len++] = c;
    pb->data[pb->len]   = '\0';
}

static void pb_puts(procbuf_t *pb, const char *s)
{
    while (*s)
        pb_putc(pb, *s++);
}

/* Minimal printf for procfs content: supports %s %u %d %x %c %% and
 * right-aligned widths with optional '0' padding, e.g. %8u %02d. */
static void pb_printf(procbuf_t *pb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            pb_putc(pb, *p);
            continue;
        }
        p++;

        int zero = 0, width = 0;
        if (*p == '0') { zero = 1; p++; }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                pb_puts(pb, s);
                break;
            }
            case 'c':
                pb_putc(pb, (char)va_arg(ap, int));
                break;
            case 'u':
            case 'd':
            case 'x': {
                int is_signed = (*p == 'd');
                long v = is_signed ? (long)va_arg(ap, int)
                                   : (long)va_arg(ap, unsigned int);
                char tmp[24];
                int  ti = 0;
                if (v == 0) {
                    tmp[ti++] = '0';
                } else {
                    if (is_signed && v < 0) {
                        pb_putc(pb, '-');
                        v = -v;
                    }
                    if (*p == 'x') {
                        static const char hex[] = "0123456789abcdef";
                        while (v) { tmp[ti++] = hex[v & 0xF]; v >>= 4; }
                    } else {
                        while (v) { tmp[ti++] = (char)('0' + (v % 10)); v /= 10; }
                    }
                }
                /* right-align to width with spaces or zeros */
                while (ti < width) {
                    pb_putc(pb, zero ? '0' : ' ');
                    width--;
                }
                while (ti > 0)
                    pb_putc(pb, tmp[--ti]);
                break;
            }
            case '%':
                pb_putc(pb, '%');
                break;
            case '\0':
                pb_putc(pb, '%');
                p--;
                break;
            default:
                pb_putc(pb, '%');
                if (*p) pb_putc(pb, *p);
                break;
        }
    }
    va_end(ap);
}


/* ============================================================================
 * File table
 * ============================================================================ */

enum {
    PROC_VERSION,
    PROC_UPTIME,
    PROC_MEMINFO,
    PROC_CPUINFO,
    PROC_LOADAVG,
    PROC_STAT,
    PROC_TASKS,
    PROC_MOUNTS,
    PROC_NFILES
};

static const char *proc_names[PROC_NFILES] = {
    "version", "uptime", "meminfo", "cpuinfo",
    "loadavg", "stat", "tasks", "mounts"
};

static const char *state_name(task_state_t s)
{
    switch (s) {
        case TASK_RUNNING: return "RUNNING";
        case TASK_READY:   return "READY";
        case TASK_BLOCKED: return "BLOCKED";
        case TASK_ZOMBIE:  return "ZOMBIE";
        case TASK_STOPPED: return "STOPPED";
        default:           return "UNKNOWN";
    }
}

static void gen_version(procbuf_t *pb)
{
    extern struct utsname sys_utsname;
    pb_printf(pb, "Scepter version %s (%s) %s\n",
              sys_utsname.release[0] ? sys_utsname.release : "0.1",
              sys_utsname.version[0] ? sys_utsname.version : "unknown",
              sys_utsname.machine);
}

static void gen_uptime(procbuf_t *pb)
{
    extern uint32_t arch_timer_get_ticks(void);
    pb_printf(pb, "%u 0\n", arch_timer_get_ticks() / 100);
}

static void gen_meminfo(procbuf_t *pb)
{
    uint32_t total_kb = buddy_total_pages() * 4;
    uint32_t free_kb  = buddy_free_pages() * 4;

    pb_printf(pb, "MemTotal:      %8u kB\n", total_kb);
    pb_printf(pb, "MemFree:       %8u kB\n", free_kb);
    pb_printf(pb, "MemAvailable:  %8u kB\n", free_kb);
    pb_printf(pb, "Buffers:       %8u kB\n", 0);
    pb_printf(pb, "Cached:        %8u kB\n", 0);
    pb_printf(pb, "SwapTotal:     %8u kB\n", 0);
    pb_printf(pb, "SwapFree:      %8u kB\n", 0);
}

static void gen_cpuinfo(procbuf_t *pb)
{
    pb_printf(pb, "processor\t: 0\n");
    pb_printf(pb, "vendor_id\t: GenuineIntel\n");
    pb_printf(pb, "model name\t: Scepter (i386)\n");
    pb_printf(pb, "cpu MHz\t\t: 0.000\n");
    pb_printf(pb, "flags\t\t: fpu tsc\n");
}

static void gen_loadavg(procbuf_t *pb)
{
    /* Single CPU, no load-average tracking yet. */
    pb_printf(pb, "0.00 0.00 0.00 1/1 0\n");
}

static void gen_stat(procbuf_t *pb)
{
    int total = 0, running = 0;
    list_head_t *head = task_list_head();
    list_head_t *pos;
    list_for_each(pos, head) {
        task_struct_t *t = list_entry(pos, task_struct_t, task_list);
        total++;
        if (t->state == TASK_RUNNING || t->state == TASK_READY)
            running++;
    }
    pb_printf(pb, "processes %u\n", total);
    pb_printf(pb, "procs_running %u\n", running);
}

static void gen_tasks(procbuf_t *pb)
{
    list_head_t *head = task_list_head();
    list_head_t *pos;
    list_for_each(pos, head) {
        task_struct_t *t = list_entry(pos, task_struct_t, task_list);
        pb_printf(pb, "%u %u %s %s %d %u %u\n",
                  t->pid, t->ppid, state_name(t->state),
                  t->name[0] ? t->name : "-", t->priority,
                  t->uticks, t->sticks);
    }
}

static void gen_mounts(procbuf_t *pb)
{
    vfs_mount_info_t mi;
    for (int i = 0; vfs_get_mount(i, &mi); i++)
        pb_printf(pb, "%s %s %s\n", mi.m_device, mi.m_path, mi.m_fstype);
}

static void (*proc_generators[PROC_NFILES])(procbuf_t *) = {
    gen_version, gen_uptime, gen_meminfo, gen_cpuinfo,
    gen_loadavg, gen_stat,   gen_tasks,   gen_mounts
};

/* ============================================================================
 * Per-open-file state
 * ============================================================================ */

typedef struct {
    char    *data;     /* generated content (NULL for the directory fd) */
    uint32_t len;
    uint32_t offset;
    int      dir_pos;  /* readdir position for the directory fd */
} procfs_file_t;

static void free_procfs_file(procfs_file_t *f)
{
    if (!f) return;
    if (f->data) kfree(f->data);
    kfree(f);
}

/* ============================================================================
 * fs_ops callbacks
 * ============================================================================ */

static int procfs_mount(int dev_id, int part_id, void **fs_private)
{
    (void)dev_id; (void)part_id;
    *fs_private = (void *)1;
    printk("[procfs] mounted at /proc\n");
    return 0;
}

static int procfs_unmount(void *fs_private)
{
    (void)fs_private;
    return 0;
}

static const char *strip_slash(const char *path)
{
    while (*path == '/') path++;
    return path;
}

static int procfs_open(void *fs_private, const char *path, int flags,
                       uint32_t mode, void **file_private)
{
    (void)fs_private; (void)mode;

    const char *name = strip_slash(path);

    procfs_file_t *f = (procfs_file_t *)kalloc(sizeof(procfs_file_t));
    if (!f) return -1;
    f->data    = NULL;
    f->len     = 0;
    f->offset  = 0;
    f->dir_pos = 0;

    /* Opening the directory itself. */
    if (*name == '\0') {
        *file_private = f;
        return 0;
    }

    if (flags & O_DIRECTORY) {
        kfree(f);
        return -1;
    }

    int idx = -1;
    for (int i = 0; i < PROC_NFILES; i++) {
        if (strcmp(proc_names[i], name) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        kfree(f);
        return -1;
    }

    /* Generate the content into a buffer. */
    procbuf_t pb = { NULL, 0, 0, 0 };
    proc_generators[idx](&pb);
    if (pb.oom || !pb.data) {
        if (pb.data) kfree(pb.data);
        kfree(f);
        return -1;
    }

    f->data = pb.data;
    f->len  = (uint32_t)pb.len;
    *file_private = f;
    return 0;
}

static int procfs_close(void *file_private)
{
    free_procfs_file((procfs_file_t *)file_private);
    return 0;
}

static int procfs_read(void *file_private, void *buf, size_t count)
{
    procfs_file_t *f = (procfs_file_t *)file_private;
    if (!f || !f->data) return -1;

    if (f->offset >= f->len)
        return 0;   /* EOF */

    size_t n = count;
    if (f->offset + n > f->len)
        n = f->len - f->offset;

    memcpy(buf, f->data + f->offset, n);
    f->offset += (uint32_t)n;
    return (int)n;
}

static int procfs_seek(void *file_private, int32_t offset, int whence,
                       uint32_t *new_offset)
{
    procfs_file_t *f = (procfs_file_t *)file_private;
    if (!f || !f->data) return -1;

    int32_t pos;
    switch (whence) {
        case SEEK_SET:
            pos = offset;
            break;
        case SEEK_CUR:
            pos = (int32_t)f->offset + offset;
            break;
        default:
            return -1;
    }
    if (pos < 0) return -1;
    f->offset = (uint32_t)pos;
    *new_offset = f->offset;
    return 0;
}

static int procfs_readdir(void *file_private, dirent_t *dirent)
{
    procfs_file_t *f = (procfs_file_t *)file_private;
    if (!f || f->data) return -1;   /* only the directory fd supports readdir */

    if (f->dir_pos >= PROC_NFILES)
        return 0;

    strncpy(dirent->name, proc_names[f->dir_pos], 255);
    dirent->name[255] = '\0';
    dirent->inode     = (uint32_t)f->dir_pos + 1;
    dirent->type      = DT_REG;
    f->dir_pos++;
    return 1;
}

static int procfs_stat(void *fs_private, const char *path, stat_t *st)
{
    (void)fs_private;

    const char *name = strip_slash(path);
    if (*name == '\0') {
        st->type  = DT_DIR;
        st->size  = 0;
        st->inode = 1;
        st->mode  = 0555;
        return 0;
    }

    for (int i = 0; i < PROC_NFILES; i++) {
        if (strcmp(proc_names[i], name) == 0) {
            st->type  = DT_REG;
            st->size  = 0;   /* generated on open */
            st->inode = (uint32_t)i + 1;
            st->mode  = 0444;
            return 0;
        }
    }
    return -1;
}

static int procfs_fstat(void *file_private, stat_t *st)
{
    procfs_file_t *f = (procfs_file_t *)file_private;
    if (!f) return -1;
    if (f->data) {
        st->type = DT_REG;
        st->size = f->len;
        st->mode = 0444;
    } else {
        st->type = DT_DIR;
        st->size = 0;
        st->mode = 0555;
    }
    st->inode = 1;
    return 0;
}

static int procfs_write(void *file_private, const void *buf, size_t count)
{
    (void)file_private; (void)buf; (void)count;
    return -1;   /* read-only */
}

static int procfs_ioctl(void *file_private, uint32_t cmd, uint32_t arg)
{
    (void)file_private; (void)cmd; (void)arg;
    return -1;
}

static int procfs_truncate(void *file_private, uint32_t length)
{
    (void)file_private; (void)length;
    return -1;
}

static int procfs_mkdir(void *fs_private, const char *path, uint32_t mode)
{
    (void)fs_private; (void)path; (void)mode;
    return -1;
}

static int procfs_rmdir(void *fs_private, const char *path)
{
    (void)fs_private; (void)path;
    return -1;
}

static int procfs_unlink(void *fs_private, const char *path)
{
    (void)fs_private; (void)path;
    return -1;
}

static int procfs_rename(void *fs_private, const char *old_path,
                         const char *new_path)
{
    (void)fs_private; (void)old_path; (void)new_path;
    return -1;
}

static fs_ops_t procfs_ops = {
    .mount    = procfs_mount,
    .unmount  = procfs_unmount,
    .open     = procfs_open,
    .close    = procfs_close,
    .read     = procfs_read,
    .write    = procfs_write,
    .seek     = procfs_seek,
    .truncate = procfs_truncate,
    .ioctl    = procfs_ioctl,
    .readdir  = procfs_readdir,
    .mkdir    = procfs_mkdir,
    .rmdir    = procfs_rmdir,
    .unlink   = procfs_unlink,
    .rename   = procfs_rename,
    .stat     = procfs_stat,
    .fstat    = procfs_fstat,
};

void procfs_init(void)
{
    if (register_pseudo_filesystem("procfs", &procfs_ops) < 0) {
        printk("[procfs] FAILED to register filesystem type\n");
        return;
    }
    printk("[procfs] filesystem type registered (%d files)\n", PROC_NFILES);
}
