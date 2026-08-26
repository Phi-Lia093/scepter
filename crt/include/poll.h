#ifndef _POLL_H
#define _POLL_H

#include <sys/types.h>

/* pollfd layout MUST match the kernel struct pollfd_k in syscall.c. */
struct pollfd {
    int   fd;
    short events;
    short revents;
};

/* Event / revents bits (Linux/POSIX values) */
#define POLLIN     0x0001
#define POLLPRI    0x0002
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020

typedef unsigned int nfds_t;

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif /* _POLL_H */
