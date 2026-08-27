#ifndef KERNEL_SYSCALL_NEW_H
#define KERNEL_SYSCALL_NEW_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Additional syscall implementations (kernel/syscall_new.c)
 *
 * Tier 1 (time/sched/fd) + Tier 2 (credentials) + Tier 3 (statfs/rlimit)
 * ========================================================================= */

/* ---- Time ---- */
int sys_time(int *user_tloc);
int sys_settimeofday(struct timeval *user_tv, void *user_tz);
int sys_utimes(const char *user_path, void *user_times);
int sys_clock_nanosleep(int clockid, int flags, void *user_rqtp, void *user_rmtp);

/* ---- Scheduling ---- */
int sys_sched_yield(void);
int sys_sched_getparam(int pid, void *user_param);
int sys_sched_setparam(int pid, void *user_param);
int sys_sched_getscheduler(int pid);
int sys_sched_get_priority_max(int policy);
int sys_sched_get_priority_min(int policy);
int sys_sched_rr_get_interval(int pid, void *user_ts);
int sys_getpriority(int which, int who);
int sys_setpriority(int which, int who, int niceval);

/* ---- Process ---- */
int sys_exit_group(int status);
int sys_tgkill(int tgid, int tid, int sig);
int sys_set_tid_address(void *cleartid_ptr);
int sys_prctl(int option, uint32_t arg2, uint32_t arg3,
              uint32_t arg4, uint32_t arg5);
int sys_personality(uint32_t persona);
int sys_chroot(const char *user_path);
int sys_flock(int fd, int op);

/* ---- File descriptors ---- */
int sys_pipe2(int *user_fds, int flags);
int sys_close_range(unsigned int first, unsigned int last, int flags);
int sys_fchdir(int fd);
int sys_sendfile(int out_fd, int in_fd, void *user_offset, size_t count);

/* ---- Filesystem ---- */
int sys_statfs(const char *user_path, void *user_buf);
int sys_fstatfs(int fd, void *user_buf);
int sys_syncfs(int fd);

/* ---- Mount / misc ---- */
int sys_umount2(const char *user_target, int flags);
int sys_fadvise64(int fd, uint32_t offset, uint32_t len, int advice);

/* ---- System info ---- */
int sys_sysinfo(void *user_info);
int sys_getrusage(int who, void *user_ru);
int sys_getrlimit(int resource, void *user_rlim);
int sys_setrlimit(int resource, void *user_rlim);

/* ---- Hostname / reboot ---- */
int sys_sethostname(const char *user_name, int len);
int sys_setdomainname(const char *user_name, int len);
int sys_reboot(uint32_t magic1, uint32_t magic2, uint32_t cmd, void *arg);

/* ---- Credentials (Tier 2) ---- */
int sys_setreuid(uint32_t ruid, uint32_t euid);
int sys_setregid(uint32_t rgid, uint32_t egid);
int sys_setresuid(uint32_t ruid, uint32_t euid, uint32_t suid);
int sys_getresuid(void *ruid, void *euid, void *suid);
int sys_setresgid(uint32_t rgid, uint32_t egid, uint32_t sgid);
int sys_getresgid(void *rgid, void *egid, void *sgid);
int sys_setfsuid(uint32_t uid);
int sys_setfsgid(uint32_t gid);
int sys_getgroups(int size, void *user_list);
int sys_setgroups(int size, void *user_list);
int sys_chown(const char *user_path, uint32_t uid, uint32_t gid);
int sys_lchown(const char *user_path, uint32_t uid, uint32_t gid);
int sys_fchown(int fd, uint32_t uid, uint32_t gid);

/* ---- Memory locking / barriers ---- */
int sys_mlock(uint32_t addr, size_t len);
int sys_munlock(uint32_t addr, size_t len);
int sys_mlockall(int flags);
int sys_munlockall(void);
int sys_membarrier(int cmd, int flags);
int sys_getcpu(void *user_cpu, void *user_node, void *cache);
int sys_getrandom(void *user_buf, size_t buflen, unsigned int flags);

#endif /* KERNEL_SYSCALL_NEW_H */
