#ifndef TTY_H
#define TTY_H

#include <stdint.h>

/* =========================================================================
 * TTY Driver - Terminal Emulator with ANSI Escape Sequence Support
 *
 * Provides a proper terminal interface with:
 * - ASCII control characters (\n, \r, \b, \t, etc.)
 * - ANSI escape sequences (cursor movement, colors, etc.)
 * - State machine for parsing escape sequences
 * ========================================================================= */

/* VGA hardware color codes */
typedef enum {
    TTY_BLACK = 0,
    TTY_BLUE = 1,
    TTY_GREEN = 2,
    TTY_CYAN = 3,
    TTY_RED = 4,
    TTY_MAGENTA = 5,
    TTY_YELLOW = 6,
    TTY_WHITE = 7,
    TTY_BRIGHT_BLACK = 8,
    TTY_BRIGHT_BLUE = 9,
    TTY_BRIGHT_GREEN = 10,
    TTY_BRIGHT_CYAN = 11,
    TTY_BRIGHT_RED = 12,
    TTY_BRIGHT_MAGENTA = 13,
    TTY_BRIGHT_YELLOW = 14,
    TTY_BRIGHT_WHITE = 15
} tty_color_t;

/**
 * Initialize TTY driver
 * Must be called after VGA init
 */
void tty_init(void);

/**
 * Write a character to TTY
 * Handles control characters and escape sequences
 */
void tty_putchar(char c);

/**
 * Write a string to TTY
 */
void tty_puts(const char *str);

/**
 * Clear the screen
 */
void tty_clear(void);

/**
 * Set foreground and background colors
 */
void tty_set_color(tty_color_t fg, tty_color_t bg);

/**
 * Get current cursor position
 */
void tty_get_cursor(uint8_t *col, uint8_t *row);

/**
 * Set cursor position (0-based)
 */
void tty_set_cursor(uint8_t col, uint8_t row);

#endif /* TTY_H */