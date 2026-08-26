#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t  pid_t;
typedef int32_t  ssize_t;
typedef uint32_t mode_t;
typedef int32_t  off_t;
typedef uint32_t ino_t;
typedef int32_t  time_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t size_t_alt;   /* not used; size_t comes from <stddef.h> */

/* termios types */
typedef uint32_t tcflag_t;
typedef uint8_t  cc_t;
typedef uint32_t speed_t;

#endif /* _SYS_TYPES_H */
