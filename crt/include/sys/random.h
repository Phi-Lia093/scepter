#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H

#include <sys/types.h>

/* getrandom(2) flags (Linux) */
#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002

/* Fill buf with buflen random bytes.  Never blocks in Scepter. */
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);

#endif /* _SYS_RANDOM_H */
