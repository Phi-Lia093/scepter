#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include <sys/types.h>
#include <time.h>

/* fd_set: a bit array of up to 256 descriptors (32 bytes). */
#define FD_SETSIZE 256

typedef struct {
    unsigned char fds_bits[FD_SETSIZE / 8];
} fd_set;

#define FD_ZERO(set)    do { \
    for (int __i = 0; __i < FD_SETSIZE / 8; __i++) \
        ((fd_set *)(set))->fds_bits[__i] = 0; \
} while (0)

#define FD_SET(fd, set)  (((fd_set *)(set))->fds_bits[(fd) >> 3] |= (1 << ((fd) & 7)))
#define FD_CLR(fd, set)  (((fd_set *)(set))->fds_bits[(fd) >> 3] &= ~(1 << ((fd) & 7)))
#define FD_ISSET(fd, set) ((((fd_set *)(set))->fds_bits[(fd) >> 3] >> ((fd) & 7)) & 1)

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);

#endif /* _SYS_SELECT_H */
