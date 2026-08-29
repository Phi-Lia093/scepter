#ifndef _UTIME_H
#define _UTIME_H

struct utimbuf {
    int32_t actime;    /* access time  */
    int32_t modtime;   /* modification time */
};

int utime(const char *path, const struct utimbuf *times);

#endif /* _UTIME_H */
