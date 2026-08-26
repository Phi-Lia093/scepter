/* ============================================================================
 * exec family + environment helpers
 *   execl, execlp, execvp, clearenv
 * ============================================================================ */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

extern char **environ;

/* Build an argv array from a NULL-terminated vararg list. */
static char **build_argv(const char *arg0, va_list ap)
{
    int n = 1;
    va_list aq;
    va_copy(aq, ap);
    while (va_arg(aq, const char *) != NULL)
        n++;
    va_end(aq);

    char **argv = (char **)malloc((size_t)(n + 1) * sizeof(char *));
    if (!argv)
        return NULL;

    argv[0] = (char *)arg0;
    for (int i = 1; i < n; i++)
        argv[i] = va_arg(ap, char *);
    argv[n] = NULL;
    return argv;
}

int execl(const char *path, const char *arg0, ...)
{
    va_list ap;
    va_start(ap, arg0);
    char **argv = build_argv(arg0, ap);
    va_end(ap);
    if (!argv) {
        errno = ENOMEM;
        return -1;
    }
    execv(path, argv);
    free(argv);
    return -1;   /* execv only returns on failure */
}

int execvp(const char *file, char *const argv[])
{
    /* If the name contains a slash, no PATH search. */
    if (strchr(file, '/'))
        return execv(file, argv);

    const char *path = getenv("PATH");
    if (!path)
        path = "/bin";

    char buf[256];
    const char *p = path;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t len = colon ? (size_t)(colon - p) : strlen(p);
        if (len == 0) { len = 1; buf[0] = '.'; }
        if (len + strlen(file) + 2 < sizeof(buf)) {
            memcpy(buf, p, len);
            buf[len] = '/';
            strcpy(buf + len + 1, file);
            execv(buf, argv);
            /* execv failed; keep searching unless it was ENOENT-like. */
        }
        if (!colon)
            break;
        p = colon + 1;
    }

    errno = ENOENT;
    return -1;
}

int execlp(const char *file, const char *arg0, ...)
{
    va_list ap;
    va_start(ap, arg0);
    char **argv = build_argv(arg0, ap);
    va_end(ap);
    if (!argv) {
        errno = ENOMEM;
        return -1;
    }
    execvp(file, argv);
    free(argv);
    return -1;
}

int clearenv(void)
{
    /* Point environ at an empty, NULL-terminated list. */
    static char *empty[] = { NULL };
    environ = empty;
    return 0;
}
