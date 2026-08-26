#include "driver/char/tty.h"
#include "driver/char/vga.h"
#include "driver/char/char.h"
#include "driver/char/pit.h"
#include "driver/char/termios.h"
#include "fs/devfs.h"
#include "kernel/signal.h"
#include "kernel/sched.h"
#include "lib/printk.h"
#include <stdint.h>

/* =========================================================================
 * TTY Constants
 * ========================================================================= */

#define TTY_WIDTH   80
#define TTY_HEIGHT  25
#define TAB_WIDTH   8

#define TTY_LINE_MAX 256   /* canonical line buffer / raw read-ahead */

/* Escape sequence parser states */
typedef enum {
    TTY_STATE_NORMAL,
    TTY_STATE_ESC,
    TTY_STATE_CSI,
} tty_state_t;

/* =========================================================================
 * TTY State
 * ========================================================================= */

static struct {
    uint8_t col;
    uint8_t row;
    tty_color_t fg;
    tty_color_t bg;
    tty_state_t state;
    int params[8];
    int param_count;
    int current_param;
    uint8_t bold;
} tty;

/* PID of the process group that owns the terminal (receives Ctrl-C etc.).
 * 0 = no owner yet; the shell sets it around each foreground child. */
static volatile int tty_foreground_pid = 0;

/* =========================================================================
 * termios state + line discipline buffers
 * ========================================================================= */

static struct kernel_termios tty_termios = {
    .c_iflag = ICRNL,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = CREAD | CS8,
    .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK,
    .c_cc = {
        [VINTR]  = 0x03,   /* ^C  */
        [VQUIT]  = 0x1C,   /* ^\  */
        [VERASE] = 0x7F,   /* DEL */
        [VKILL]  = 0x15,   /* ^U  */
        [VEOF]   = 0x04,   /* ^D  */
        [VMIN]   = 1,
        [VTIME]  = 0,
        [VSUSP]  = 0x1A,   /* ^Z  */
        [VSTART] = 0x11,
        [VSTOP]  = 0x13,
        [VEOL]   = 0,
    },
};

/* canonical mode: a completed line being consumed by read() */
static char     tty_line_buf[TTY_LINE_MAX];
static int      tty_line_len   = 0;   /* bytes accumulated in the line    */
static int      tty_line_pos   = 0;   /* read position into completed line */
static int      tty_line_ready = 0;   /* 1 = a full line is available      */

/* raw mode: bytes pre-buffered by VMIN accumulation / the poll pump */
static char     tty_raw_buf[TTY_LINE_MAX];
static int      tty_raw_len    = 0;
static int      tty_raw_pos    = 0;

/* =========================================================================
 * User pointer helpers (defined in kernel/syscall.c)
 * ========================================================================= */

extern int copy_from_user(void *kernel_dst, const void *user_src, size_t n);
extern int copy_to_user(void *user_dst, const void *kernel_src, size_t n);

/* =========================================================================
 * VGA Backend Functions
 * ========================================================================= */

static void tty_write_cell(uint8_t col, uint8_t row, char c, uint8_t color)
{
    volatile uint16_t *vga = (volatile uint16_t *)0xC00B8000;
    vga[row * TTY_WIDTH + col] = (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

static void tty_update_hw_cursor(void)
{
    vga_set_cursor(tty.col, tty.row);
}

static uint8_t tty_make_color(tty_color_t fg, tty_color_t bg)
{
    return (uint8_t)(fg | (bg << 4));
}

/* =========================================================================
 * TTY Operations
 * ========================================================================= */

void tty_clear(void)
{
    /* Hand the VGA console over to userspace: after the first user clear,
     * the kernel stops writing to the screen (serial only). */
    extern int kernel_vga_enabled;
    kernel_vga_enabled = 0;

    uint8_t color = tty_make_color(tty.fg, tty.bg);
    for (int row = 0; row < TTY_HEIGHT; row++)
        for (int col = 0; col < TTY_WIDTH; col++)
            tty_write_cell(col, row, ' ', color);
    tty.col = 0;
    tty.row = 0;
    tty_update_hw_cursor();
}

void tty_set_color(tty_color_t fg, tty_color_t bg)
{
    tty.fg = fg;
    tty.bg = bg;
}

void tty_get_cursor(uint8_t *col, uint8_t *row)
{
    if (col) *col = tty.col;
    if (row) *row = tty.row;
}

void tty_set_cursor(uint8_t col, uint8_t row)
{
    if (col >= TTY_WIDTH)  col = TTY_WIDTH  - 1;
    if (row >= TTY_HEIGHT) row = TTY_HEIGHT - 1;
    tty.col = col;
    tty.row = row;
    tty_update_hw_cursor();
}

static void tty_scroll(void)
{
    uint8_t color = tty_make_color(tty.fg, tty.bg);
    volatile uint16_t *vga = (volatile uint16_t *)0xC00B8000;

    for (int row = 1; row < TTY_HEIGHT; row++)
        for (int col = 0; col < TTY_WIDTH; col++)
            vga[(row - 1) * TTY_WIDTH + col] = vga[row * TTY_WIDTH + col];

    for (int col = 0; col < TTY_WIDTH; col++)
        tty_write_cell(col, TTY_HEIGHT - 1, ' ', color);

    if (tty.row > 0) tty.row--;
}

static void tty_newline(void)
{
    tty.col = 0;
    tty.row++;
    if (tty.row >= TTY_HEIGHT)
        tty_scroll();
}

/* =========================================================================
 * ANSI Escape Sequence Parser
 * ========================================================================= */

static void tty_execute_csi(char command)
{
    int n, m;

    switch (command) {
    case 'A':
        n = (tty.param_count > 0 && tty.params[0] > 0) ? tty.params[0] : 1;
        if (tty.row >= n) tty.row -= n; else tty.row = 0;
        break;
    case 'B':
        n = (tty.param_count > 0 && tty.params[0] > 0) ? tty.params[0] : 1;
        tty.row += n;
        if (tty.row >= TTY_HEIGHT) tty.row = TTY_HEIGHT - 1;
        break;
    case 'C':
        n = (tty.param_count > 0 && tty.params[0] > 0) ? tty.params[0] : 1;
        tty.col += n;
        if (tty.col >= TTY_WIDTH) tty.col = TTY_WIDTH - 1;
        break;
    case 'D':
        n = (tty.param_count > 0 && tty.params[0] > 0) ? tty.params[0] : 1;
        if (tty.col >= n) tty.col -= n; else tty.col = 0;
        break;
    case 'H':
    case 'f':
        n = (tty.param_count > 0 && tty.params[0] > 0) ? tty.params[0] - 1 : 0;
        m = (tty.param_count > 1 && tty.params[1] > 0) ? tty.params[1] - 1 : 0;
        tty_set_cursor(m, n);
        return;
    case 'J':
        n = (tty.param_count > 0) ? tty.params[0] : 0;
        if (n == 2) { tty_clear(); return; }
        break;
    case 'K':
        {
            uint8_t color = tty_make_color(tty.fg, tty.bg);
            for (int col = tty.col; col < TTY_WIDTH; col++)
                tty_write_cell(col, tty.row, ' ', color);
        }
        break;
    case 'm':
        for (int i = 0; i < tty.param_count; i++) {
            int param = tty.params[i];
            if (param == 0) {
                tty.fg = TTY_WHITE; tty.bg = TTY_BLACK; tty.bold = 0;
            } else if (param == 1) {
                tty.bold = 1;
            } else if (param >= 30 && param <= 37) {
                static const uint8_t ansi_to_vga[] = {0,4,2,6,1,5,3,7};
                tty.fg = (tty_color_t)(ansi_to_vga[param - 30] + (tty.bold ? 8 : 0));
            } else if (param >= 40 && param <= 47) {
                static const uint8_t ansi_to_vga[] = {0,4,2,6,1,5,3,7};
                tty.bg = (tty_color_t)ansi_to_vga[param - 40];
            }
        }
        break;
    }

    tty_update_hw_cursor();
}

/* =========================================================================
 * Character Output
 * ========================================================================= */

void tty_putchar(char c)
{
    uint8_t color = tty_make_color(tty.fg, tty.bg);

    switch (tty.state) {
    case TTY_STATE_NORMAL:
        if (c == '\033') { tty.state = TTY_STATE_ESC; return; }
        switch (c) {
        case '\n': tty_newline(); break;
        case '\r': tty.col = 0; break;
        case '\b':
            if (tty.col > 0) { tty.col--; tty_write_cell(tty.col, tty.row, ' ', color); }
            break;
        case '\t':
            {
                int next_tab = ((tty.col / TAB_WIDTH) + 1) * TAB_WIDTH;
                if (next_tab >= TTY_WIDTH) {
                    tty_newline();
                } else {
                    while (tty.col < next_tab) {
                        tty_write_cell(tty.col, tty.row, ' ', color);
                        tty.col++;
                    }
                }
            }
            break;
        case '\a': break;
        default:
            if (c >= 32 && c <= 126) {
                tty_write_cell(tty.col, tty.row, c, color);
                tty.col++;
                if (tty.col >= TTY_WIDTH) tty_newline();
            }
            break;
        }
        break;

    case TTY_STATE_ESC:
        if (c == '[') {
            tty.state = TTY_STATE_CSI;
            tty.param_count   = 0;
            tty.current_param = 0;
            for (int i = 0; i < 8; i++) tty.params[i] = 0;
        } else {
            tty.state = TTY_STATE_NORMAL;
        }
        break;

    case TTY_STATE_CSI:
        if (c >= '0' && c <= '9') {
            tty.current_param = tty.current_param * 10 + (c - '0');
        } else if (c == ';') {
            if (tty.param_count < 8)
                tty.params[tty.param_count++] = tty.current_param;
            tty.current_param = 0;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            if (tty.param_count < 8)
                tty.params[tty.param_count++] = tty.current_param;
            tty_execute_csi(c);
            tty.state = TTY_STATE_NORMAL;
        } else {
            tty.state = TTY_STATE_NORMAL;
        }
        break;
    }

    tty_update_hw_cursor();
}

void tty_puts(const char *str)
{
    while (*str) tty_putchar(*str++);
}

/* =========================================================================
 * termios / line discipline
 * ========================================================================= */

#define TTY_IOCTL_CLEAR  0x1
#define TTY_IOCTL_SET_FG 0x2
#define TTY_IOCTL_GET_FG 0x3

/* Echo one character to the VGA console (bypasses OPOST processing:
 * the echo is what the user typed, so '\n' is rendered as CR+LF). */
static void tty_echo_char(int c)
{
    if (c == '\n')
        tty_putchar('\r');
    tty_putchar((char)c);
}

/* Deliver an ISIG control character to the foreground process.
 * Returns 1 if the byte was consumed (signal sent), 0 if it should be
 * passed through as data (e.g. no foreground process, or ISIG off). */
int tty_handle_control(int ascii)
{
    if (!(tty_termios.c_lflag & ISIG))
        return 0;

    int fg = tty_foreground_pid;
    int signum = 0;

    if (ascii == tty_termios.c_cc[VINTR])
        signum = SIGINT;
    else if (ascii == tty_termios.c_cc[VQUIT])
        signum = SIGQUIT;
    else if (ascii == tty_termios.c_cc[VSUSP])
        signum = SIGTSTP;

    if (signum == 0)
        return 0;

    if (fg > 0) {
        extern int send_signal(uint32_t pid, int signum);
        send_signal((uint32_t)fg, signum);
        return 1;    /* consumed: do not enqueue */
    }
    return 0;        /* no foreground process: let the reader see it */
}

/* Erase the last character of the canonical line (echo if ECHOE). */
static void tty_line_erase(void)
{
    if (tty_line_len <= 0)
        return;
    tty_line_len--;
    if ((tty_termios.c_lflag & ECHO) && (tty_termios.c_lflag & ECHOE)) {
        tty_puts("\b \b");
    } else if (tty_termios.c_lflag & ECHO) {
        tty_echo_char(tty_termios.c_cc[VERASE]);
    }
}

/* Kill the whole canonical line (echo if ECHOE|ECHOK). */
static void tty_line_kill(void)
{
    if (tty_termios.c_lflag & ECHO) {
        if (tty_termios.c_lflag & (ECHOE | ECHOK)) {
            for (int i = 0; i < tty_line_len; i++)
                tty_puts("\b \b");
        } else {
            tty_echo_char(tty_termios.c_cc[VKILL]);
        }
    }
    tty_line_len = 0;
}

/* Reset the input buffers (used by TCSETSF / at init). */
static void tty_reset_input(void)
{
    tty_line_len = 0;
    tty_line_pos = 0;
    tty_line_ready = 0;
    tty_raw_len = 0;
    tty_raw_pos = 0;
}

/* Set the foreground process PID (the one that receives Ctrl-C). */
void tty_set_foreground(int pid)
{
    tty_foreground_pid = pid;
}

/* PID of the current foreground process, or 0 if none. */
int tty_get_foreground(void)
{
    return tty_foreground_pid;
}

/* Process one raw input byte through the termios state machine.
 * May echo, send a signal, or buffer the byte. */
static void tty_process_byte(int c)
{
    uint32_t lflag = tty_termios.c_lflag;
    uint32_t iflag = tty_termios.c_iflag;

    /* ISIG: ^C / ^\ / ^Z -> signals to the foreground process. */
    if (lflag & ISIG) {
        if (c == tty_termios.c_cc[VINTR] ||
            c == tty_termios.c_cc[VQUIT] ||
            c == tty_termios.c_cc[VSUSP]) {
            if (tty_handle_control(c)) {
                /* byte consumed as a signal; discard a partial line */
                if (lflag & ICANON)
                    tty_line_len = 0;
                return;
            }
        }
    }

    if (lflag & ICANON) {
        /* ---- canonical mode ---- */
        if (c == tty_termios.c_cc[VERASE] || c == 0x08) {
            tty_line_erase();
            return;
        }
        if (c == tty_termios.c_cc[VKILL]) {
            tty_line_kill();
            return;
        }
        /* EOF at start of line -> mark an empty "line" so read() sees 0. */
        if (tty_termios.c_cc[VEOF] && c == tty_termios.c_cc[VEOF]) {
            if (tty_line_len == 0) {
                tty_line_buf[0] = '\0';
                tty_line_len = 0;
                tty_line_pos = 0;
                tty_line_ready = 1;
            }
            return;   /* EOF mid-line is ignored */
        }
        /* \r -> \n (ICRNL) */
        if (c == '\r' && (iflag & ICRNL))
            c = '\n';
        if (c == '\n' || (tty_termios.c_cc[VEOL] && c == tty_termios.c_cc[VEOL])) {
            if ((lflag & ECHO) || (lflag & ECHONL))
                tty_echo_char('\n');
            if (tty_line_len < TTY_LINE_MAX - 1) {
                tty_line_buf[tty_line_len] = '\n';
                tty_line_len++;
            }
            tty_line_pos = 0;
            tty_line_ready = 1;
            return;
        }
        if (c >= 32 && c < 127) {
            if (tty_line_len < TTY_LINE_MAX - 1) {
                tty_line_buf[tty_line_len++] = (char)c;
                if (lflag & ECHO)
                    tty_echo_char(c);
            }
            return;
        }
        /* other control chars are discarded in canonical mode */
        return;
    }

    /* ---- raw (non-canonical) mode ---- */
    if (tty_raw_len < TTY_LINE_MAX) {
        tty_raw_buf[tty_raw_len++] = (char)c;
        if (tty_termios.c_lflag & ECHO)
            tty_echo_char(c);
    }
}

/* Drain any keyboard bytes that are already available without blocking.
 * Used by poll() so select()/poll() reflect line-discipline readiness. */
static void tty_pump(void)
{
    while (char_poll(3, 0)) {
        int c = cread(3, 0);
        if (c <= 0)
            break;
        tty_process_byte(c);
        if ((tty_termios.c_lflag & ICANON) && tty_line_ready)
            break;
    }
}

/* Read one byte through the line discipline, blocking as needed. */
static int tty_read(int scnd_id)
{
    (void)scnd_id;

    for (;;) {
        /* 1. Return bytes from a completed canonical line. */
        if (tty_line_ready) {
            int c = (uint8_t)tty_line_buf[tty_line_pos++];
            if (tty_line_pos >= tty_line_len) {
                tty_line_ready = 0;
                tty_line_pos = 0;
                tty_line_len = 0;
            }
            return c;
        }
        /* 2. Return pre-buffered raw bytes. */
        if (tty_raw_pos < tty_raw_len)
            return (uint8_t)tty_raw_buf[tty_raw_pos++];

        /* 3. Drain whatever the keyboard already has. */
        tty_pump();
        if (tty_line_ready)
            continue;
        if (tty_raw_pos < tty_raw_len)
            return (uint8_t)tty_raw_buf[tty_raw_pos++];

        /* 4. Block for more input. */
        if (tty_termios.c_lflag & ICANON) {
            int c = char_read_block(3, 0);
            if (c < 0)
                return c;                  /* -EINTR */
            tty_process_byte(c);
            /* loop: the processed byte may have completed a line */
        } else {
            /* raw mode: honor VMIN (bytes) and VTIME (0.1s inter-byte). */
            unsigned vmin  = tty_termios.c_cc[VMIN];
            unsigned vtime = tty_termios.c_cc[VTIME];
            if (vmin == 0)
                vmin = 1;
            if (vmin == 1 && vtime == 0) {
                int c = char_read_block(3, 0);
                if (c < 0)
                    return c;
                return c;
            }
            /* accumulate up to VMIN bytes (VTIME = timeout between bytes) */
            uint32_t start = pit_get_ticks();
            uint32_t limit = vtime ? start + (uint32_t)vtime * 10 : 0;
            while ((unsigned)tty_raw_len < vmin) {
                if (char_poll(3, 0)) {
                    int c = cread(3, 0);
                    if (c > 0) {
                        tty_raw_buf[tty_raw_len++] = (char)c;
                        start = pit_get_ticks();
                        limit = vtime ? start + (uint32_t)vtime * 10 : 0;
                        continue;
                    }
                }
                if (vtime && tty_raw_len > 0 && pit_get_ticks() >= limit)
                    break;
                int c = char_read_block(3, 0);
                if (c < 0)
                    return c;
                if (c > 0) {
                    tty_raw_buf[tty_raw_len++] = (char)c;
                    start = pit_get_ticks();
                    limit = vtime ? start + (uint32_t)vtime * 10 : 0;
                }
            }
            if (tty_raw_len > 0)
                return (uint8_t)tty_raw_buf[tty_raw_pos++];
            return 0;   /* timed out with no data */
        }
    }
}

static int tty_poll(int scnd_id)
{
    (void)scnd_id;
    /* Drive the line discipline with whatever is already buffered, so a
     * select()/poll() on the tty reflects true read() readiness. */
    tty_pump();
    if (tty_line_ready)
        return 1;
    if (tty_raw_pos < tty_raw_len)
        return 1;
    return 0;
}

static int tty_write_cb(int scnd_id, char c)
{
    (void)scnd_id;
    if ((tty_termios.c_oflag & OPOST) && (tty_termios.c_oflag & ONLCR) &&
        c == '\n') {
        tty_putchar('\r');
        tty_putchar('\n');
    } else {
        tty_putchar(c);
    }
    return 0;
}

static int tty_ioctl(int prim_id, int scnd_id, unsigned int command, uint32_t arg)
{
    (void)prim_id;
    (void)scnd_id;

    switch (command) {
    case TTY_IOCTL_CLEAR:
        tty_clear();
        return 0;
    case TTY_IOCTL_SET_FG:
        tty_set_foreground((int)arg);
        return 0;
    case TTY_IOCTL_GET_FG:
        return tty_get_foreground();
    case TCGETS:
        if (copy_to_user((void *)arg, &tty_termios,
                         sizeof(tty_termios)) < 0)
            return -1;
        return 0;
    case TCSETS:
    case TCSETSW:
        if (copy_from_user(&tty_termios, (void *)arg,
                           sizeof(tty_termios)) < 0)
            return -1;
        return 0;
    case TCSETSF:   /* flush input on change */
        if (copy_from_user(&tty_termios, (void *)arg,
                           sizeof(tty_termios)) < 0)
            return -1;
        tty_reset_input();
        return 0;
    case TIOCGWINSZ: {
        struct kernel_winsize ws = { TTY_HEIGHT, TTY_WIDTH, 0, 0 };
        if (copy_to_user((void *)arg, &ws, sizeof(ws)) < 0)
            return -1;
        return 0;
    }
    case TIOCSPGRP:
        tty_set_foreground((int)arg);
        return 0;
    case TIOCGPGRP:
        return tty_get_foreground();
    default:
        return -1;
    }
}

/* =========================================================================
 * Initialisation – state reset + driver registration + devfs node
 * ========================================================================= */

void tty_init(void)
{
    tty.col          = 0;
    tty.row          = 0;
    tty.fg           = TTY_WHITE;
    tty.bg           = TTY_BLACK;
    tty.state        = TTY_STATE_NORMAL;
    tty.param_count  = 0;
    tty.current_param = 0;
    tty.bold         = 0;

    tty_reset_input();

    char_ops_t ops = { .read = tty_read, .write = tty_write_cb, .poll = tty_poll, .ioctl = tty_ioctl };
    register_char_device(2, &ops);
    devfs_register_device("tty0", DT_CHRDEV, 2, 0);
}