#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

/* ioctl commands for /dev/tty0 (must match driver/char/tty.c) */
#define IOCTL_TTY_CLEAR  1   /* clear screen and reset cursor   */
#define IOCTL_TTY_SET_FG 2   /* set foreground process PID      */
#define IOCTL_TTY_GET_FG 3   /* get foreground process PID      */

/* ioctl commands for /dev/rtc0 (must match driver/char/rtc.c) */
#define IOCTL_RTC_GET_TIME 1  /* return current Unix time (int) */

/* ioctl commands for /dev/pcspk (must match driver/char/pcspk.h) */
#define IOCTL_PCSPK_BEEP 1   /* arg = frequency in Hz; 0 = silence */
#define IOCTL_PCSPK_GET  2   /* return current frequency */

int ioctl(int fd, unsigned int cmd, unsigned int arg);

#endif /* _SYS_IOCTL_H */
