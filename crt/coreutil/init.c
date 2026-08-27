/* ============================================================================
 * init - PID 1
 * Automounts devfs at /dev, opens the console as stdin/stdout/stderr, then
 * spawns and respawns /bin/sh, reaping each shell as it exits.
 * ============================================================================ */

#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "sys/ioctl.h"
#include "sys/wait.h"
#include "sys/stat.h"
#include "sys/mount.h"
#include "errno.h"

int main(int argc, char *argv[], char *envp[])
{
    (void)argc;
    (void)argv;

    char *sh_argv[] = { "sh", NULL };

    /* ---- Automount devfs at /dev -------------------------------------
     * The kernel registers the "devfs" filesystem type but does not mount
     * it (like Linux devtmpfs).  We create the mount point and mount it
     * here, before opening any device node.  On a respawn (init is reused
     * after the shell exits) /dev is already mounted and mount() returns
     * EBUSY, which is fine. */
    mkdir("/dev", 0755);
    if (mount("devfs", "/dev", "devfs", 0, NULL) < 0) {
        if (errno != EBUSY)
            printf("init: devfs mount failed (errno=%d)\n", errno);
    }

    /* ---- Automount procfs at /proc and tmpfs at /tmp ---------------- */
    mkdir("/proc", 0755);
    if (mount("procfs", "/proc", "procfs", 0, NULL) < 0) {
        if (errno != EBUSY)
            printf("init: procfs mount failed (errno=%d)\n", errno);
    }
    mkdir("/tmp", 01777);
    if (mount("tmpfs", "/tmp", "tmpfs", 0, NULL) < 0) {
        if (errno != EBUSY)
            printf("init: tmpfs mount failed (errno=%d)\n", errno);
    }

    /* Set up the console as fd 0/1/2 */
    if (open("/dev/tty0", O_RDWR) < 0) {
        printf("init: cannot open /dev/tty0\n");
        exit(1);
    }
    dup(0);
    dup(0);
    ioctl(STDOUT_FILENO, IOCTL_TTY_CLEAR, 0);

    printf("Scepter OS - init running\n");

    for (;;) {
        pid_t pid = fork();
        if (pid < 0) {
            printf("init: fork failed\n");
            break;
        }
        if (pid == 0) {
            /* Child: become the shell */
            execve("/bin/sh", sh_argv, envp);
            printf("init: failed to exec /bin/sh\n");
            exit(1);
        }
        /* Parent: wait for the shell, then respawn it */
        int status;
        waitpid(pid, &status, 0);
    }

    for (;;) {}
}
