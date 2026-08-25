/* ============================================================================
 * init - PID 1
 * Opens the console as stdin/stdout/stderr, then spawns and respawns
 * /bin/sh, reaping each shell as it exits.
 * ============================================================================ */

#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "sys/ioctl.h"
#include "sys/wait.h"

int main(int argc, char *argv[], char *envp[])
{
    (void)argc;
    (void)argv;

    char *sh_argv[] = { "sh", NULL };

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
