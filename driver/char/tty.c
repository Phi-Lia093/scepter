#include "driver/char/tty.h"
#include "driver/char/vga.h"
#include "driver/char/char.h"
#include "fs/devfs.h"
#include <stdint.h>

/* =========================================================================
 * TTY Constants
 * ========================================================================= */

#define TTY_WIDTH   80
#define TTY_HEIGHT  25
#define TAB_WIDTH   8

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
 * Driver callbacks
 * ========================================================================= */

#define TTY_IOCTL_CLEAR  0x1

static char tty_read(int scnd_id)
{
    (void)scnd_id;
    return cread(3, 0);   /* forward to keyboard (char dev 3) */
}

static int tty_write_cb(int scnd_id, char c)
{
    (void)scnd_id;
    tty_putchar(c);
    return 0;
}

static int tty_ioctl(int prim_id, int scnd_id, unsigned int command)
{
    (void)prim_id;
    (void)scnd_id;
    if (command == TTY_IOCTL_CLEAR) { tty_clear(); return 0; }
    return -1;
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

    char_ops_t ops = { .read = tty_read, .write = tty_write_cb, .ioctl = tty_ioctl };
    register_char_device(2, &ops);
    devfs_register_device("tty0", DT_CHRDEV, 2, 0);
}