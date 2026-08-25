#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

/* waitpid() options */
#define WNOHANG   1
#define WUNTRACED 2
#define WCONTINUED 8

/* Status decoding.  The kernel stores the wait status in the conventional
 * low-7-bits layout:
 *   - exit(code)             -> (code & 0xff) << 8   (low 7 bits == 0)
 *   - killed by signal sig   -> sig in the low 7 bits
 *   - stopped by signal sig  -> (sig << 8) | 0x7f
 *   - continued (WCONTINUED) -> 0xffff */
#define WIFEXITED(s)   (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) ((((s) & 0x7f) != 0) && (((s) & 0x7f) != 0x7f))
#define WTERMSIG(s)    ((s) & 0x7f)
#define WIFSTOPPED(s)  (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)    (((s) >> 8) & 0xff)
#define WIFCONTINUED(s) ((s) == 0xffff)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);

#endif /* _SYS_WAIT_H */
