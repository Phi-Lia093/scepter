/* ============================================================================
 * Miscellaneous libc functions: realpath, ttyname, gethostname,
 * sysconf/pathconf, getrlimit/setrlimit, perror, fileno, fdopen, getline.
 * ============================================================================ */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/resource.h>

/* ---- perror / fileno / fdopen ---- */

void perror(const char *s)
{
    if (s && *s)
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    else
        fprintf(stderr, "%s\n", strerror(errno));
}

int fileno(FILE stream)
{
    return (int)stream;
}

FILE fdopen(int fd, const char *mode)
{
    (void)mode;
    return (FILE)fd;
}

/* ---- getline: read a line (including '\n') from stream ---- */

ssize_t getline(char **lineptr, size_t *n, FILE stream)
{
    if (!lineptr || !n) {
        errno = EINVAL;
        return -1;
    }

    size_t cap = *n;
    if (!*lineptr || cap == 0) {
        cap = 128;
        *lineptr = (char *)malloc(cap);
        if (!*lineptr) {
            errno = ENOMEM;
            return -1;
        }
        *n = cap;
    }

    size_t len = 0;
    for (;;) {
        if (len + 2 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(*lineptr, cap);
            if (!nb) {
                errno = ENOMEM;
                return -1;
            }
            *lineptr = nb;
            *n = cap;
        }
        char c;
        ssize_t r = read(stream, &c, 1);
        if (r < 0)
            return -1;
        if (r == 0) {
            if (len == 0)
                return -1;   /* EOF with no data */
            (*lineptr)[len] = '\0';
            return (ssize_t)len;
        }
        (*lineptr)[len++] = c;
        if (c == '\n') {
            (*lineptr)[len] = '\0';
            return (ssize_t)len;
        }
    }
}

/* ---- realpath: normalize a path ---- */

char *realpath(const char *path, char *resolved)
{
    static char buf[256];
    char *out = resolved ? resolved : buf;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }

    char cwd[256];
    if (path[0] != '/') {
        if (!getcwd(cwd, sizeof(cwd))) {
            errno = EACCES;
            return NULL;
        }
    } else {
        cwd[0] = '\0';
    }

    /* Compose an absolute path. */
    char tmp[512];
    if (path[0] == '/')
        strncpy(tmp, path, sizeof(tmp) - 1);
    else
        snprintf(tmp, sizeof(tmp), "%s/%s", cwd, path);
    tmp[sizeof(tmp) - 1] = '\0';

    /* Normalize '.' and '..' components. */
    char stack[32][256];
    int depth = 0;
    char *save = NULL;
    for (char *tok = strtok_r(tmp, "/", &save); tok;
         tok = strtok_r(NULL, "/", &save)) {
        if (strcmp(tok, ".") == 0 || *tok == '\0')
            continue;
        if (strcmp(tok, "..") == 0) {
            if (depth > 0)
                depth--;
            continue;
        }
        if (depth < 32)
            strncpy(stack[depth++], tok, 255);
    }

    out[0] = '/';
    out[1] = '\0';
    for (int i = 0; i < depth; i++) {
        if (strlen(out) + strlen(stack[i]) + 2 < 256) {
            strcat(out, stack[i]);
            strcat(out, "/");
        }
    }
    /* Strip trailing slash except for root. */
    size_t l = strlen(out);
    if (l > 1 && out[l - 1] == '/')
        out[l - 1] = '\0';

    /* Verify it exists (unless the caller just wants normalization). */
    struct stat st;
    if (stat(out, &st) < 0) {
        errno = ENOENT;
        return NULL;
    }
    return out;
}

/* ---- ttyname ---- */

char *ttyname(int fd)
{
    if (isatty(fd))
        return "/dev/tty0";
    return NULL;
}

/* ---- gethostname / sethostname ---- */

int gethostname(char *name, size_t len)
{
    struct utsname u;
    if (uname(&u) < 0)
        return -1;
    strncpy(name, u.nodename, len);
    name[len - 1] = '\0';
    return 0;
}

/* ---- sysconf / pathconf ---- */

long sysconf(int name)
{
    switch (name) {
        case _SC_CLK_TCK:            return 100;
        case _SC_PAGESIZE:           return 4096;
        case _SC_NPROCESSORS_ONLN:   return 1;
        case _SC_OPEN_MAX:           return 1024;
        case _SC_ARG_MAX:            return 65536;
        case _SC_PHYS_PAGES:         return 32768;   /* 128 MB / 4 KB */
        case _SC_CHILD_MAX:          return 64;
        default:
            errno = EINVAL;
            return -1;
    }
}

long pathconf(const char *path, int name)
{
    (void)path;
    switch (name) {
        case _PC_NAME_MAX:     return 30;
        case _PC_PATH_MAX:     return 4096;
        case _PC_LINK_MAX:     return 16;
        default:
            errno = EINVAL;
            return -1;
    }
}
