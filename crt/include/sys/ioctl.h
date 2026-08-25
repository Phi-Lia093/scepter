#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

/* ioctl commands for /dev/tty0 (must match driver/char/tty.c) */
#define IOCTL_TTY_CLEAR  1   /* clear screen and reset cursor   */
#define IOCTL_TTY_SET_FG 2   /* set foreground process PID      */
#define IOCTL_TTY_GET_FG 3   /* get foreground process PID      */

int ioctl(int fd, unsigned int cmd, unsigned int arg);

#endif /* _SYS_IOCTL_H */
