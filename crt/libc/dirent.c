/* ============================================================================
 * Directory stream API (opendir/readdir/closedir) over the getdents() syscall
 * ============================================================================ */

#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

DIR *opendir(const char *path)
{
    if (!path) return NULL;
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return NULL;
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) {
        close(fd);
        return NULL;
    }
    d->fd = fd;
    return d;
}

struct dirent *readdir(DIR *d)
{
    if (!d) return NULL;
    if (getdents(d->fd, &d->cur, 1) != 1)
        return NULL;
    return &d->cur;
}

int closedir(DIR *d)
{
    if (!d) return -1;
    int r = close(d->fd);
    free(d);
    return r;
}
