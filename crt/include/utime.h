#ifndef _UTIME_H
#define _UTIME_H

struct utimbuf {
    long actime;    /* access time  */
    long modtime;   /* modification time */
};

int utime(const char *path, const struct utimbuf *times);

#endif /* _UTIME_H */
