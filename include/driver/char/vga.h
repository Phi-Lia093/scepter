#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include "driver/char/tty.h"

/* =========================================================================
 * VGA colour definitions
 * ========================================================================= */

typedef enum {
    VGA_BLACK         = 0,
    VGA_BLUE          = 1,
    VGA_GREEN         = 2,
    VGA_CYAN          = 3,
    VGA_RED           = 4,
    VGA_MAGENTA       = 5,
    VGA_BROWN         = 6,
    VGA_LIGHT_GREY    = 7,
    VGA_DARK_GREY     = 8,
    VGA_LIGHT_BLUE    = 9,
    VGA_LIGHT_GREEN   = 10,
    VGA_LIGHT_CYAN    = 11,
    VGA_LIGHT_RED     = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_LIGHT_BROWN   = 14,
    VGA_WHITE         = 15,
} vga_color_t;

/* Pack foreground and background into one attribute byte */
static inline uint8_t vga_entry_color(vga_color_t fg, vga_color_t bg)
{
    return (uint8_t)(fg | (bg << 4));
}

/* Pack a character and its attribute into a 16-bit VGA cell */
static inline uint16_t vga_entry(char c, uint8_t color)
{
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

/* =========================================================================
 * VGA text-mode interface (char device – one character at a time)
 * ========================================================================= */

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t color);
void vga_putchar(char c);
void vga_set_cursor(int col, int row);
void vga_get_cursor(uint8_t *col, uint8_t *row);

/* Route kernel-console output (printk / vga_putchar) to a different console,
 * e.g. the graphics console once a VBE mode is active. */
void console_set_putchar(void (*fn)(char));

/* Fill the tty output backend for the legacy VGA text console (80x25). */
void vga_fill_tty_backend(tty_backend_t *be);

#endif /* VGA_H */
