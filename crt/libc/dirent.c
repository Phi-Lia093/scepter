/* ============================================================================
 * Directory stream API (opendir/readdir/closedir) over the getdents() syscall
 * ============================================================================ */

#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

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

/* ============================================================================
 * rewinddir / seekdir / telldir / scandir / alphasort
 * ============================================================================ */

void rewinddir(DIR *d)
{
    if (!d) return;
    lseek(d->fd, 0, SEEK_SET);
}

void seekdir(DIR *d, long offset)
{
    if (!d) return;
    lseek(d->fd, (off_t)offset, SEEK_SET);
}

long telldir(DIR *d)
{
    (void)d;
    /* The kernel does not expose the directory position. */
    return -1;
}

int alphasort(const struct dirent **a, const struct dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

int scandir(const char *path, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **))
{
    DIR *d = opendir(path);
    if (!d)
        return -1;

    int n = 0, cap = 16;
    struct dirent **list = (struct dirent **)malloc((size_t)cap * sizeof(*list));
    if (!list) {
        closedir(d);
        return -1;
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (filter && !filter(e))
            continue;
        if (n >= cap) {
            cap *= 2;
            struct dirent **nb = (struct dirent **)realloc(
                list, (size_t)cap * sizeof(*list));
            if (!nb) {
                for (int i = 0; i < n; i++)
                    free(list[i]);
                free(list);
                closedir(d);
                return -1;
            }
            list = nb;
        }
        struct dirent *copy = (struct dirent *)malloc(sizeof(struct dirent));
        if (!copy) {
            for (int i = 0; i < n; i++)
                free(list[i]);
            free(list);
            closedir(d);
            return -1;
        }
        *copy = *e;
        list[n++] = copy;
    }
    closedir(d);

    if (!compar)
        compar = alphasort;
    if (n > 1)
        qsort(list, (size_t)n, sizeof(*list),
              (int (*)(const void *, const void *))compar);

    *namelist = list;
    return n;
}
