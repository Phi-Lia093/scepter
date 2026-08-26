#ifndef TERMIOS_H
#define TERMIOS_H

#include <stdint.h>

/* =========================================================================
 * Kernel termios definitions (mirrored by crt/include/termios.h).
 *
 * Values follow Linux i386 so user programs written against the libc
 * header behave identically.  Only the subset the TTY driver understands
 * is honoured; the rest are stored but ignored.
 * ========================================================================= */

#define NCCS 32

/* c_cc[] indices */
#define VINTR   0
#define VQUIT   1
#define VERASE  2
#define VKILL   3
#define VEOF    4
#define VTIME   5
#define VMIN    6
#define VSWTC   7
#define VSTART  8
#define VSTOP   9
#define VSUSP   10
#define VEOL    11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT  15
#define VEOL2   16

/* c_iflag */
#define IGNBRK  0x0001
#define BRKINT  0x0002
#define IGNPAR  0x0004
#define PARMRK  0x0008
#define INPCK   0x0010
#define ISTRIP  0x0020
#define INLCR   0x0040
#define IGNCR   0x0080
#define ICRNL   0x0100
#define IUCLC   0x0200
#define IXON    0x0400
#define IXANY   0x0800
#define IXOFF   0x1000
#define IMAXBEL 0x2000
#define IUTF8   0x4000

/* c_oflag */
#define OPOST   0x0001
#define OLCUC   0x0002
#define ONLCR   0x0004
#define OCRNL   0x0008
#define ONOCR   0x0010
#define ONLRET  0x0020
#define OFILL   0x0040
#define OFDEL   0x0080

/* c_cflag */
#define CSIZE   0x0030
#define CS5     0x0000
#define CS6     0x0010
#define CS7     0x0020
#define CS8     0x0030
#define CSTOPB  0x0040
#define CREAD   0x0080
#define PARENB  0x0100
#define PARODD  0x0200
#define HUPCL   0x0400
#define CLOCAL  0x0800

/* c_lflag */
#define ISIG    0x0001
#define ICANON  0x0002
#define XCASE   0x0004
#define ECHO    0x0008
#define ECHOE   0x0010
#define ECHOK   0x0020
#define ECHONL  0x0040
#define NOFLSH  0x0080
#define TOSTOP  0x0100
#define ECHOCTL 0x0200
#define ECHOPRT 0x0400
#define ECHOKE  0x0800
#define FLUSHO  0x1000
#define PENDIN  0x2000
#define IEXTEN  0x8000

/* termios structure - layout must match crt/include/termios.h */
struct kernel_termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_cc[NCCS];
};

/* ioctl commands (Linux-compatible) */
#define TCGETS    0x5401
#define TCSETS    0x5402
#define TCSETSW   0x5403
#define TCSETSF   0x5404
#define TCGETA    0x5405
#define TCSETA    0x5406
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCOUTQ  0x5411
#define TIOCSTI   0x5412
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414

/* struct winsize (Linux) */
struct kernel_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

#endif /* TERMIOS_H */
