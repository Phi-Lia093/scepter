/* ============================================================================
 * Additional System Call Implementations
 *
 * Tier 1 (time/sched/fd/misc), Tier 2 (credentials), Tier 3 (statfs/rlimit).
 * ============================================================================ */

#include "kernel/syscall.h"
#include "kernel/syscall_new.h"
#include "kernel/sched.h"
#include "kernel/process.h"
#include "kernel/signal.h"
#include "fs/fs.h"
#include "driver/char/pit.h"
#include "driver/char/rtc.h"
#include "mm/buddy.h"
#include "lib/printk.h"
#include "lib/string.h"
#include "errno.h"

/* ---- helpers defined in kernel/syscall.c ---- */
extern int valid_user_pointer(const void *ptr, size_t len);
extern int copy_from_user(void *kernel_dst, const void *user_src, size_t n);
extern int copy_to_user(void *user_dst, const void *kernel_src, size_t n);

/* CLOCK_* clock ids (also defined in kernel/syscall.c) */
#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2

extern uint32_t pit_get_ticks(void);
extern uint32_t rtc_get_boot_unix_time(void);
extern void do_exit(int status);
extern void schedule(void);

/* ============================================================================
 * Kernel ABI structures (must match crt/include/)
 * ============================================================================ */

/* Linux i386 struct sysinfo */
struct sysinfo_k {
    uint32_t uptime;          /* seconds since boot                        */
    uint32_t loads[3];        /* 1/2/15 minute load averages               */
    uint32_t totalram;        /* total usable main memory (bytes)          */
    uint32_t freeram;
    uint32_t sharedram;
    uint32_t bufferram;
    uint32_t totalswap;
    uint32_t freeswap;
    uint16_t procs;           /* number of current processes               */
    uint16_t pad;
    uint32_t totalhigh;
    uint32_t freehigh;
    uint32_t mem_unit;        /* memory unit size in bytes                 */
    char     _f[8];
};

/* Linux i386 struct rusage */
struct rusage_k {
    struct timeval ru_utime;
    struct timeval ru_stime;
    uint32_t ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt,
             ru_nswap, ru_inblock, ru_oublock, ru_msgsnd, ru_msgrcv,
             ru_nsignals, ru_nvcsw, ru_nivcsw;
};

/* Linux struct rlimit (32-bit) */
struct rlimit_k {
    uint32_t rlim_cur;
    uint32_t rlim_max;
};

/* Linux struct sched_param */
struct sched_param_k {
    int sched_priority;
};

/* ============================================================================
 * Time
 * ============================================================================ */

/* sys_time - Seconds since epoch (wall clock). */
int sys_time(int *user_tloc)
{
    uint32_t now = rtc_get_boot_unix_time() + pit_get_ticks() / 100;

    if (user_tloc) {
        if (!valid_user_pointer(user_tloc, sizeof(int)))
            return -EFAULT;
        if (copy_to_user(user_tloc, &now, sizeof(now)) < 0)
            return -EFAULT;
    }
    return (int)now;
}

/* sys_settimeofday - Set the wall clock (root only).
 *
 * Only the time value is honoured; the timezone pointer is ignored
 * (consistent with modern Linux where tz is deprecated and usually NULL).
 * The kernel stores a fixed offset applied on top of (RTC boot time +
 * PIT uptime) so CLOCK_REALTIME/gettimeofday/time() all see the change.
 */
int sys_settimeofday(struct timeval *user_tv, void *user_tz)
{
    if (current->euid != 0)
        return -EPERM;

    if (!user_tv || !valid_user_pointer(user_tv, sizeof(struct timeval)))
        return -EFAULT;

    struct timeval tv;
    if (copy_from_user(&tv, user_tv, sizeof(tv)) < 0)
        return -EFAULT;

    if (tv.tv_usec < 0 || tv.tv_usec >= 1000000)
        return -EINVAL;

    uint32_t now = rtc_get_real_boot_unix_time() + pit_get_ticks() / 100;
    int32_t  delta = tv.tv_sec - (int32_t)now;
    rtc_set_time_offset(delta);

    (void)user_tz;
    return 0;
}

/* sys_utimes - Set atime/mtime with a pair of timevals (NULL = now). */
int sys_utimes(const char *user_path, void *user_times)
{
    if (!valid_user_pointer(user_path, 1))
        return -EFAULT;

    char path[MAX_PATH_LEN];
    extern int copy_path_from_user(const char *user, char *kern, size_t n);
    if (copy_path_from_user(user_path, path, sizeof(path)) < 0)
        return -EFAULT;

    uint32_t atime, mtime;
    if (user_times) {
        if (!valid_user_pointer(user_times, 2 * sizeof(struct timeval)))
            return -EFAULT;
        struct timeval tv[2];
        if (copy_from_user(tv, user_times, sizeof(tv)) < 0)
            return -EFAULT;
        atime = (uint32_t)tv[0].tv_sec;
        mtime = (uint32_t)tv[1].tv_sec;
    } else {
        uint32_t now = (uint32_t)rtc_get_boot_unix_time() + pit_get_ticks() / 100;
        atime = mtime = now;
    }

    if (fs_utime(path, atime, mtime) < 0)
        return -EINVAL;
    return 0;
}

/* sys_clock_nanosleep - Sleep on a clock.  Only CLOCK_REALTIME and
 * CLOCK_MONOTONIC are supported; TIMER_ABSTIME computes the remaining
 * interval from the given absolute target. */
int sys_clock_nanosleep(int clockid, int flags, void *user_rqtp,
                        void *user_rmtp)
{
    if (!valid_user_pointer(user_rqtp, sizeof(timespec_t)))
        return -EFAULT;

    timespec_t rqtp;
    if (copy_from_user(&rqtp, user_rqtp, sizeof(rqtp)) < 0)
        return -EFAULT;

    if (clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC)
        return -EINVAL;

    if (flags & 1) {   /* TIMER_ABSTIME */
        /* remaining = target - now */
        uint32_t now_ms = pit_get_ticks() * 10;   /* ms since boot */
        uint32_t target_ms = (uint32_t)rqtp.tv_sec * 1000
                           + (uint32_t)rqtp.tv_nsec / 1000000;
        if ((int32_t)(now_ms - target_ms) >= 0)
            return 0;   /* already passed */
        rqtp.tv_sec  = (target_ms - now_ms) / 1000;
        rqtp.tv_nsec = (target_ms - now_ms) % 1000 * 1000000;
    }

    /* Sleep for the requested interval.  (Implemented here, not by
     * delegating to sys_nanosleep: that entry validates its pointers as
     * user pointers, while rqtp is a kernel-side copy.) */
    if (rqtp.tv_sec < 0 || rqtp.tv_nsec < 0 || rqtp.tv_nsec >= 1000000000L)
        return -EINVAL;

    uint32_t total_ticks = (uint32_t)rqtp.tv_sec * 100UL
                         + (uint32_t)((rqtp.tv_nsec + 9999999L) / 10000000L);
    uint32_t target = pit_get_ticks() + total_ticks;

    while (pit_get_ticks() < target) {
        extern wait_queue_head_t timer_wq;
        sleep_on(&timer_wq);
        if (current->pending)
            return -EINTR;
    }

    /* Remaining time is zero (full duration slept). */
    if (user_rmtp && valid_user_pointer(user_rmtp, sizeof(timespec_t))) {
        timespec_t rem = { 0, 0 };
        copy_to_user(user_rmtp, &rem, sizeof(rem));
    }
    return 0;
}

/* ============================================================================
 * Scheduling
 * ============================================================================ */

int sys_sched_yield(void)
{
    schedule();
    return 0;
}

static int task_of_pid(int pid, task_struct_t **out)
{
    task_struct_t *task;
    if (pid == 0)
        task = current;
    else {
        task = find_task_by_pid((uint32_t)pid);
        if (!task)
            return -ESRCH;
    }
    *out = task;
    return 0;
}

int sys_sched_getparam(int pid, void *user_param)
{
    task_struct_t *task;
    int r = task_of_pid(pid, &task);
    if (r) return r;

    if (!valid_user_pointer(user_param, sizeof(struct sched_param_k)))
        return -EFAULT;
    struct sched_param_k p;
    p.sched_priority = task->priority;   /* our "nice"-like priority */
    if (copy_to_user(user_param, &p, sizeof(p)) < 0)
        return -EFAULT;
    return 0;
}

int sys_sched_setparam(int pid, void *user_param)
{
    task_struct_t *task;
    int r = task_of_pid(pid, &task);
    if (r) return r;

    if (task != current && current->euid != 0)
        return -EPERM;
    if (!valid_user_pointer(user_param, sizeof(struct sched_param_k)))
        return -EFAULT;

    struct sched_param_k p;
    if (copy_from_user(&p, user_param, sizeof(p)) < 0)
        return -EFAULT;
    if (p.sched_priority < -20) p.sched_priority = -20;
    if (p.sched_priority > 19)  p.sched_priority = 19;
    task->priority = p.sched_priority;
    return 0;
}

int sys_sched_getscheduler(int pid)
{
    task_struct_t *task;
    int r = task_of_pid(pid, &task);
    if (r) return r;
    return 0;   /* SCHED_OTHER */
}

int sys_sched_get_priority_max(int policy)
{
    (void)policy;
    return 0;   /* SCHED_OTHER has a single priority */
}

int sys_sched_get_priority_min(int policy)
{
    (void)policy;
    return 0;
}

int sys_sched_rr_get_interval(int pid, void *user_ts)
{
    task_struct_t *task;
    int r = task_of_pid(pid, &task);
    if (r) return r;

    if (!valid_user_pointer(user_ts, sizeof(timespec_t)))
        return -EFAULT;
    timespec_t ts = { 0, 10000000 };   /* 10 ms timeslice */
    if (copy_to_user(user_ts, &ts, sizeof(ts)) < 0)
        return -EFAULT;
    return 0;
}

/* getpriority / setpriority: which = PRIO_PROCESS(0)/PGRP(1)/USER(2) */
int sys_getpriority(int which, int who)
{
    int pid = who;
    switch (which) {
        case 0: break;                       /* PRIO_PROCESS */
        case 1: return 0;                    /* PRIO_PGRP: return our nice */
        case 2: return 0;                    /* PRIO_USER */
        default: return -EINVAL;
    }
    task_struct_t *task;
    int r = task_of_pid(pid, &task);
    if (r) return r;
    return task->priority;
}

int sys_setpriority(int which, int who, int niceval)
{
    if (which < 0 || which > 2)
        return -EINVAL;
    if (current->euid != 0)
        return -EPERM;   /* only root may renice (simple model) */

    if (niceval < -20) niceval = -20;
    if (niceval > 19)  niceval = 19;

    switch (which) {
        case 0: {
            int pid = who ? who : (int)current->pid;
            task_struct_t *task = find_task_by_pid((uint32_t)pid);
            if (!task) return -ESRCH;
            task->priority = niceval;
            return 0;
        }
        case 1:
        case 2: {
            /* Apply to all tasks in the group / all user tasks. */
            extern list_head_t *task_list_head(void);
            list_head_t *pos;
            list_for_each(pos, task_list_head()) {
                task_struct_t *t = list_entry(pos, task_struct_t, task_list);
                if (t->pid == 0) continue;
                if (which == 1 && who != 0 && t->pgid != (uint32_t)who)
                    continue;
                if (which == 2 && who != 0 && t->uid != (uint32_t)who)
                    continue;
                t->priority = niceval;
            }
            return 0;
        }
    }
    return -EINVAL;
}

/* ============================================================================
 * Process
 * ============================================================================ */

int sys_exit_group(int status)
{
    do_exit(status);
    return 0;   /* never reached */
}

int sys_tgkill(int tgid, int tid, int sig)
{
    if (tgid != tid)
        return -EINVAL;   /* no threads yet: tgid must equal tid */
    return sys_kill(tid, sig);
}

int sys_set_tid_address(void *cleartid_ptr)
{
    if (cleartid_ptr && !valid_user_pointer(cleartid_ptr, sizeof(int)))
        return -EFAULT;
    current->cleartid = (uint32_t)cleartid_ptr;
    return (int)current->pid;
}

/* prctl - only PR_SET_NAME / PR_GET_NAME are supported. */
int sys_prctl(int option, uint32_t arg2, uint32_t arg3,
              uint32_t arg4, uint32_t arg5)
{
    (void)arg3; (void)arg4; (void)arg5;

    switch (option) {
        case 15: {   /* PR_SET_NAME */
            char name[17];
            if (!valid_user_pointer((void *)arg2, 16))
                return -EFAULT;
            if (copy_from_user(name, (void *)arg2, 16) < 0)
                return -EFAULT;
            name[16] = '\0';
            strncpy(current->name, name, sizeof(current->name) - 1);
            current->name[sizeof(current->name) - 1] = '\0';
            return 0;
        }
        case 16: {   /* PR_GET_NAME */
            if (!valid_user_pointer((void *)arg2, 16))
                return -EFAULT;
            char name[16];
            strncpy(name, current->name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            if (copy_to_user((void *)arg2, name, sizeof(name)) < 0)
                return -EFAULT;
            return 0;
        }
        default:
            return -EINVAL;
    }
}

int sys_personality(uint32_t persona)
{
    uint32_t old = current->personality;
    current->personality = persona;
    return (int)old;
}

/* ============================================================================
 * File descriptors
 * ============================================================================ */

int sys_pipe2(int *user_fds, int flags)
{
    if (!valid_user_pointer(user_fds, 2 * sizeof(int)))
        return -EFAULT;

    int fds[2];
    if (fs_pipe(fds) < 0)
        return -ENFILE;

    /* Apply O_CLOEXEC (per-fd) and O_NONBLOCK (per open file). */
    if (flags & O_CLOEXEC)
        fs_fd_set_cloexec(fds[0]);
    if (flags & O_CLOEXEC)
        fs_fd_set_cloexec(fds[1]);
    if (flags & O_NONBLOCK) {
        fs_fd_set_nonblock(fds[0]);
        fs_fd_set_nonblock(fds[1]);
    }

    if (copy_to_user(user_fds, fds, 2 * sizeof(int)) < 0)
        return -EFAULT;
    return 0;
}

int sys_close_range(unsigned int first, unsigned int last, int flags)
{
    (void)flags;   /* CLOSE_RANGE_UNSHARE: no sharing model, ignore */
    if (first > last)
        return -EINVAL;
    return fs_close_range(first, last);
}

int sys_fchdir(int fd)
{
    return fs_fchdir(fd);
}

/* sendfile - copy count bytes from in_fd to out_fd (offset advances). */
int sys_sendfile(int out_fd, int in_fd, void *user_offset, size_t count)
{
    if (!fs_fd_valid(out_fd) || !fs_fd_valid(in_fd))
        return -EBADF;
    if (user_offset && !valid_user_pointer(user_offset, sizeof(uint32_t)))
        return -EFAULT;

    char buf[2048];
    size_t total = 0;
    uint32_t offset = 0;

    if (user_offset) {
        if (copy_from_user(&offset, user_offset, sizeof(offset)) < 0)
            return -EFAULT;
    }

    while (total < count) {
        size_t chunk = count - total;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);

        long n;
        if (user_offset)
            n = fs_pread(in_fd, buf, chunk, offset);
        else
            n = fs_read(in_fd, buf, chunk);
        if (n <= 0) break;

        long w = fs_write(out_fd, buf, (size_t)n);
        if (w < 0) {
            if (total) break;
            return (int)w;
        }
        if (user_offset)
            offset += (uint32_t)w;
        total += (size_t)w;
        if ((size_t)w < (size_t)n) break;   /* short write */
    }

    if (user_offset && copy_to_user(user_offset, &offset, sizeof(offset)) < 0)
        return -EFAULT;

    return (int)total;
}

/* ============================================================================
 * Filesystem statistics / mount / misc fs
 * ============================================================================ */

int sys_statfs(const char *user_path, void *user_buf)
{
    if (!valid_user_pointer(user_path, 1))
        return -EFAULT;
    if (!valid_user_pointer(user_buf, sizeof(fs_statfs_t)))
        return -EFAULT;

    char path[MAX_PATH_LEN];
    extern int copy_path_from_user(const char *user, char *kern, size_t n);
    if (copy_path_from_user(user_path, path, sizeof(path)) < 0)
        return -EFAULT;

    fs_statfs_t sb;
    if (fs_statfs(path, &sb) < 0)
        return -ENOENT;
    if (copy_to_user(user_buf, &sb, sizeof(sb)) < 0)
        return -EFAULT;
    return 0;
}

int sys_fstatfs(int fd, void *user_buf)
{
    if (!valid_user_pointer(user_buf, sizeof(fs_statfs_t)))
        return -EFAULT;

    fs_statfs_t sb;
    if (fs_fstatfs(fd, &sb) < 0)
        return -EBADF;
    if (copy_to_user(user_buf, &sb, sizeof(sb)) < 0)
        return -EFAULT;
    return 0;
}

int sys_syncfs(int fd)
{
    (void)fd;
    extern int sys_sync(void);
    return sys_sync();
}

int sys_umount2(const char *user_target, int flags)
{
    (void)flags;   /* MNT_FORCE/MNT_DETACH not supported; same as umount */
    extern int sys_umount(const char *target);
    return sys_umount(user_target);
}

int sys_fadvise64(int fd, uint32_t offset, uint32_t len, int advice)
{
    (void)fd; (void)offset; (void)len; (void)advice;
    return 0;   /* POSIX_FADV_NORMAL: single disk, no cache policy */
}

/* ============================================================================
 * System information / resource usage
 * ============================================================================ */

/* Count user processes (for sysinfo). */
static int count_user_procs(void)
{
    int n = 0;
    extern list_head_t *task_list_head(void);
    list_head_t *pos;
    list_for_each(pos, task_list_head()) {
        task_struct_t *t = list_entry(pos, task_struct_t, task_list);
        if (t->pid != 0 && t->state != TASK_ZOMBIE)
            n++;
    }
    return n;
}

int sys_sysinfo(void *user_info)
{
    if (!valid_user_pointer(user_info, sizeof(struct sysinfo_k)))
        return -EFAULT;

    struct sysinfo_k info;
    info.uptime   = pit_get_ticks() / 100;
    info.loads[0] = info.loads[1] = info.loads[2] = 0;
    info.totalram = buddy_total_pages() * 4096U;
    info.freeram  = buddy_free_pages() * 4096U;
    info.sharedram = 0;
    info.bufferram = 0;
    info.totalswap = 0;
    info.freeswap  = 0;
    info.procs     = (uint16_t)count_user_procs();
    info.pad       = 0;
    info.totalhigh = 0;
    info.freehigh  = 0;
    info.mem_unit  = 1;
    memset(info._f, 0, sizeof(info._f));

    if (copy_to_user(user_info, &info, sizeof(info)) < 0)
        return -EFAULT;
    return 0;
}

int sys_getrusage(int who, void *user_ru)
{
    if (who != 0 && who != 1)   /* RUSAGE_SELF / RUSAGE_CHILDREN */
        return -EINVAL;
    if (!valid_user_pointer(user_ru, sizeof(struct rusage_k)))
        return -EFAULT;

    struct rusage_k ru;
    memset(&ru, 0, sizeof(ru));

    uint32_t uticks = current->uticks;
    uint32_t sticks = current->sticks;
    if (who == 1)   /* RUSAGE_CHILDREN: no per-child accounting yet */
        uticks = sticks = 0;

    ru.ru_utime.tv_sec  = uticks / 100;
    ru.ru_utime.tv_usec = (uticks % 100) * 10000;
    ru.ru_stime.tv_sec  = sticks / 100;
    ru.ru_stime.tv_usec = (sticks % 100) * 10000;

    if (copy_to_user(user_ru, &ru, sizeof(ru)) < 0)
        return -EFAULT;
    return 0;
}

int sys_getrlimit(int resource, void *user_rlim)
{
    if (resource < 0 || resource >= RLIM_NLIMITS)
        return -EINVAL;
    if (!valid_user_pointer(user_rlim, sizeof(struct rlimit_k)))
        return -EFAULT;

    struct rlimit_k rl;
    rl.rlim_cur = current->rlimit_cur[resource];
    rl.rlim_max = current->rlimit_max[resource];
    if (copy_to_user(user_rlim, &rl, sizeof(rl)) < 0)
        return -EFAULT;
    return 0;
}

int sys_setrlimit(int resource, void *user_rlim)
{
    if (resource < 0 || resource >= RLIM_NLIMITS)
        return -EINVAL;
    if (!valid_user_pointer(user_rlim, sizeof(struct rlimit_k)))
        return -EFAULT;

    struct rlimit_k rl;
    if (copy_from_user(&rl, user_rlim, sizeof(rl)) < 0)
        return -EFAULT;

    /* Non-root may only lower limits, never raise above the hard limit. */
    if (current->euid != 0) {
        if (rl.rlim_max > current->rlimit_max[resource])
            return -EPERM;
        if (rl.rlim_cur > current->rlimit_max[resource])
            return -EPERM;
    }

    current->rlimit_cur[resource] = rl.rlim_cur;
    current->rlimit_max[resource] = rl.rlim_max;
    return 0;
}

/* ============================================================================
 * Hostname / reboot
 * ============================================================================ */

static int copy_name_from_user(const char *user_name, int len,
                               char *out, size_t outsz)
{
    if (len < 0 || len >= (int)outsz)
        return -EINVAL;
    if (len == 0)
        len = 1;   /* allow empty name -> zero-length string */
    if (!valid_user_pointer(user_name, (size_t)len))
        return -EFAULT;
    if (copy_from_user(out, user_name, (size_t)len) < 0)
        return -EFAULT;
    out[len] = 0;
    return 0;
}

int sys_sethostname(const char *user_name, int len)
{
    if (current->euid != 0)
        return -EPERM;
    char buf[65];
    int r = copy_name_from_user(user_name, len, buf, sizeof(buf));
    if (r) return r;
    memset(sys_utsname.nodename, 0, sizeof(sys_utsname.nodename));
    strncpy(sys_utsname.nodename, buf, sizeof(sys_utsname.nodename) - 1);
    return 0;
}

int sys_setdomainname(const char *user_name, int len)
{
    if (current->euid != 0)
        return -EPERM;
    char buf[65];
    int r = copy_name_from_user(user_name, len, buf, sizeof(buf));
    if (r) return r;
    /* Store the domain name after the machine field; uname doesn't expose
     * a domainname field in this 5-field struct, so keep it in version. */
    memset(sys_utsname.version, 0, sizeof(sys_utsname.version));
    strncpy(sys_utsname.version, buf, sizeof(sys_utsname.version) - 1);
    return 0;
}

int sys_reboot(uint32_t magic1, uint32_t magic2, uint32_t cmd, void *arg)
{
    (void)arg;
    if (current->euid != 0)
        return -EPERM;
    if (magic1 != 0xfee1dead)
        return -EINVAL;
    if (magic2 != 0x28121969 && magic2 != 0x5171986 &&
        magic2 != 0x01234567 && magic2 != 0x20112004)
        return -EINVAL;

    printk("[SYSCALL] reboot cmd=0x%x\n", cmd);

    /* Power off: QEMU/Bochs ACPI port. */
    if (cmd == 0x4321fedc) {   /* LINUX_REBOOT_CMD_POWER_OFF */
        __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000),
                         "Nd"((uint16_t)0x604));
    }
    /* QEMU reboot uses the 0x64/0x60 keyboard controller reset. */
    if (cmd == 0x1234567) {    /* LINUX_REBOOT_CMD_RESTART */
        uint8_t val = 0xFE;
        __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"((uint16_t)0x64));
    }

    /* Halt (never returns). */
    for (;;)
        __asm__ volatile("hlt");
}

/* ============================================================================
 * Credentials (Tier 2)
 * ============================================================================ */

int sys_setreuid(uint32_t ruid, uint32_t euid)
{
    task_struct_t *t = current;
    if (t->euid != 0) {
        if (ruid != 0xFFFFFFFFU && ruid != t->uid && ruid != t->euid &&
            ruid != t->suid)
            return -EPERM;
        if (euid != 0xFFFFFFFFU && euid != t->uid && euid != t->euid &&
            euid != t->suid)
            return -EPERM;
    }
    if (ruid != 0xFFFFFFFFU) {
        t->uid  = ruid;
        if (euid == 0xFFFFFFFFU)
            t->euid = ruid;
    }
    if (euid != 0xFFFFFFFFU)
        t->euid = euid;
    t->suid = t->euid;
    return 0;
}

int sys_setregid(uint32_t rgid, uint32_t egid)
{
    task_struct_t *t = current;
    if (t->egid != 0) {
        if (rgid != 0xFFFFFFFFU && rgid != t->gid && rgid != t->egid &&
            rgid != t->sgid)
            return -EPERM;
        if (egid != 0xFFFFFFFFU && egid != t->gid && egid != t->egid &&
            egid != t->sgid)
            return -EPERM;
    }
    if (rgid != 0xFFFFFFFFU) {
        t->gid  = rgid;
        if (egid == 0xFFFFFFFFU)
            t->egid = rgid;
    }
    if (egid != 0xFFFFFFFFU)
        t->egid = egid;
    t->sgid = t->egid;
    return 0;
}

int sys_setresuid(uint32_t ruid, uint32_t euid, uint32_t suid)
{
    task_struct_t *t = current;
    if (t->euid != 0) {
        if (ruid != 0xFFFFFFFFU && ruid != t->uid && ruid != t->euid &&
            ruid != t->suid)
            return -EPERM;
        if (euid != 0xFFFFFFFFU && euid != t->uid && euid != t->euid &&
            euid != t->suid)
            return -EPERM;
        if (suid != 0xFFFFFFFFU && suid != t->uid && suid != t->euid &&
            suid != t->suid)
            return -EPERM;
    }
    if (ruid != 0xFFFFFFFFU) t->uid  = ruid;
    if (euid != 0xFFFFFFFFU) t->euid = euid;
    if (suid != 0xFFFFFFFFU) t->suid = suid;
    return 0;
}

int sys_getresuid(void *ruid, void *euid, void *suid)
{
    task_struct_t *t = current;
    if (ruid && copy_to_user(ruid, &t->uid, 4) < 0) return -EFAULT;
    if (euid && copy_to_user(euid, &t->euid, 4) < 0) return -EFAULT;
    if (suid && copy_to_user(suid, &t->suid, 4) < 0) return -EFAULT;
    return 0;
}

int sys_setresgid(uint32_t rgid, uint32_t egid, uint32_t sgid)
{
    task_struct_t *t = current;
    if (t->egid != 0) {
        if (rgid != 0xFFFFFFFFU && rgid != t->gid && rgid != t->egid &&
            rgid != t->sgid)
            return -EPERM;
        if (egid != 0xFFFFFFFFU && egid != t->gid && egid != t->egid &&
            egid != t->sgid)
            return -EPERM;
        if (sgid != 0xFFFFFFFFU && sgid != t->gid && sgid != t->egid &&
            sgid != t->sgid)
            return -EPERM;
    }
    if (rgid != 0xFFFFFFFFU) t->gid  = rgid;
    if (egid != 0xFFFFFFFFU) t->egid = egid;
    if (sgid != 0xFFFFFFFFU) t->sgid = sgid;
    return 0;
}

int sys_getresgid(void *rgid, void *egid, void *sgid)
{
    task_struct_t *t = current;
    if (rgid && copy_to_user(rgid, &t->gid, 4) < 0) return -EFAULT;
    if (egid && copy_to_user(egid, &t->egid, 4) < 0) return -EFAULT;
    if (sgid && copy_to_user(sgid, &t->sgid, 4) < 0) return -EFAULT;
    return 0;
}

int sys_setfsuid(uint32_t uid)
{
    uint32_t old = current->fsuid;
    if (uid != current->uid && uid != current->euid &&
        uid != current->suid && current->euid != 0)
        return -EPERM;
    current->fsuid = uid;
    return (int)old;
}

int sys_setfsgid(uint32_t gid)
{
    uint32_t old = current->fsgid;
    if (gid != current->gid && gid != current->egid &&
        gid != current->sgid && current->euid != 0)
        return -EPERM;
    current->fsgid = gid;
    return (int)old;
}

int sys_getgroups(int size, void *user_list)
{
    int n = (int)current->ngroups;
    if (user_list == NULL) {
        /* Return the number of groups without writing the list. */
        if (size < 0) return -EINVAL;
        return n;
    }
    if (size < n)
        return -EINVAL;
    if (!valid_user_pointer(user_list, (size_t)n * 4))
        return -EFAULT;
    if (n > 0 && copy_to_user(user_list, current->groups, (size_t)n * 4) < 0)
        return -EFAULT;
    return n;
}

int sys_setgroups(int size, void *user_list)
{
    if (current->euid != 0)
        return -EPERM;
    if (size < 0 || size > 16)
        return -EINVAL;
    if (size > 0) {
        if (!valid_user_pointer(user_list, (size_t)size * 4))
            return -EFAULT;
        uint32_t list[16];
        if (copy_from_user(list, user_list, (size_t)size * 4) < 0)
            return -EFAULT;
        memcpy(current->groups, list, (size_t)size * 4);
    }
    current->ngroups = (uint32_t)size;
    return 0;
}

int sys_chown(const char *user_path, uint32_t uid, uint32_t gid)
{
    if (current->euid != 0)
        return -EPERM;
    extern int copy_path_from_user(const char *user, char *kern, size_t n);
    char path[MAX_PATH_LEN];
    if (!valid_user_pointer(user_path, 1))
        return -EFAULT;
    if (copy_path_from_user(user_path, path, sizeof(path)) < 0)
        return -EFAULT;
    int ruid = (uid == 0xFFFFFFFFU) ? -1 : (int)uid;
    int rgid = (gid == 0xFFFFFFFFU) ? -1 : (int)gid;
    return fs_chown(path, ruid, rgid);
}

int sys_lchown(const char *user_path, uint32_t uid, uint32_t gid)
{
    /* lchown == chown here (symlinks are not followed by either). */
    return sys_chown(user_path, uid, gid);
}

int sys_fchown(int fd, uint32_t uid, uint32_t gid)
{
    if (current->euid != 0)
        return -EPERM;
    int ruid = (uid == 0xFFFFFFFFU) ? -1 : (int)uid;
    int rgid = (gid == 0xFFFFFFFFU) ? -1 : (int)gid;
    return fs_fchown(fd, ruid, rgid);
}

/* ============================================================================
 * Memory locking / barriers / cpu
 * ============================================================================ */

int sys_mlock(uint32_t addr, size_t len)
{
    (void)addr; (void)len;
    return 0;   /* no swap: pages are never evicted */
}

int sys_munlock(uint32_t addr, size_t len)
{
    (void)addr; (void)len;
    return 0;
}

int sys_mlockall(int flags)
{
    (void)flags;
    return 0;
}

int sys_munlockall(void)
{
    return 0;
}

int sys_membarrier(int cmd, int flags)
{
    (void)flags;
    /* MEMBARRIER_CMD_QUERY = 0: report supported commands. */
    if (cmd == 0)
        return 0;   /* no barrier commands needed (single CPU, no MT) */
    return -EINVAL;
}

int sys_getcpu(void *user_cpu, void *user_node, void *cache)
{
    (void)cache;
    uint32_t zero = 0;
    if (user_cpu && copy_to_user(user_cpu, &zero, 4) < 0) return -EFAULT;
    if (user_node && copy_to_user(user_node, &zero, 4) < 0) return -EFAULT;
    return 0;
}

/* getrandom(2) flags (Linux) */
#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002

/* sys_getrandom - Fill a user buffer with random bytes.
 *
 * With no real entropy hardware this kernel treats /dev/urandom semantics
 * as the only source; GRND_RANDOM is accepted (it never blocks here, so
 * GRND_NONBLOCK is irrelevant), and flags are otherwise validated. */
int sys_getrandom(void *user_buf, size_t buflen, unsigned int flags)
{
    if (flags & ~(GRND_NONBLOCK | GRND_RANDOM))
        return -EINVAL;
    if (buflen == 0)
        return 0;
    if (!valid_user_pointer(user_buf, buflen))
        return -EFAULT;

    extern void random_get_bytes(void *buf, size_t n);
    /* Generate into kernel memory first (copy_to_user validates + writes). */
    uint8_t tmp[64];
    size_t done = 0;
    while (done < buflen) {
        size_t chunk = buflen - done;
        if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
        random_get_bytes(tmp, chunk);
        if (copy_to_user((uint8_t *)user_buf + done, tmp, chunk) < 0)
            return (done > 0) ? (int)done : -EFAULT;
        done += chunk;
    }
    return (int)done;
}

/* sys_chroot - Change the calling process's root directory (root only). */
int sys_chroot(const char *user_path)
{
    if (current->euid != 0)
        return -EPERM;

    if (!valid_user_pointer(user_path, 1))
        return -EFAULT;

    char path[MAX_PATH_LEN];
    extern int copy_path_from_user(const char *user, char *kern, size_t n);
    if (copy_path_from_user(user_path, path, sizeof(path)) < 0)
        return -EFAULT;

    return fs_chroot(path);
}

/* sys_flock - Apply/remove an advisory lock on an open file. */
int sys_flock(int fd, int op)
{
    return fs_flock(fd, op);
}
