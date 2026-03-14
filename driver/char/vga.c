#include "driver/char/vga.h"
#include "driver/char/char.h"
#include "fs/devfs.h"
#include "asm.h"

/* =========================================================================
 * VGA text-mode constants
 * ========================================================================= */

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_BUFFER  ((volatile uint16_t *)0xC00B8000)

/* VGA CRT controller I/O ports */
#define VGA_CTRL_REG  0x3D4
#define VGA_DATA_REG  0x3D5
#define VGA_CURSOR_HI 0x0E
#define VGA_CURSOR_LO 0x0F

/* =========================================================================
 * Driver state
 * ========================================================================= */

static uint8_t vga_col   = 0;
static uint8_t vga_row   = 0;
static uint8_t vga_color = 0;  /* set by vga_init */

/* =========================================================================
 * Hardware cursor
 * ========================================================================= */

void vga_set_cursor(uint8_t col, uint8_t row)
{
    uint16_t pos = (uint16_t)row * VGA_WIDTH + col;
    outb(VGA_CTRL_REG, VGA_CURSOR_HI);
    outb(VGA_DATA_REG, (uint8_t)(pos >> 8));
    outb(VGA_CTRL_REG, VGA_CURSOR_LO);
    outb(VGA_DATA_REG, (uint8_t)(pos & 0xFF));
    vga_col = col;
    vga_row = row;
}

void vga_get_cursor(uint8_t *col, uint8_t *row)
{
    outb(VGA_CTRL_REG, VGA_CURSOR_HI);
    uint16_t pos = (uint16_t)inb(VGA_DATA_REG) << 8;
    outb(VGA_CTRL_REG, VGA_CURSOR_LO);
    pos |= inb(VGA_DATA_REG);
    *col = (uint8_t)(pos % VGA_WIDTH);
    *row = (uint8_t)(pos / VGA_WIDTH);
}

/* =========================================================================
 * Screen operations
 * ========================================================================= */

void vga_set_color(uint8_t color)
{
    vga_color = color;
}

void vga_clear(void)
{
    uint16_t blank = vga_entry(' ', vga_color);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = blank;
    vga_set_cursor(0, 0);
}

/* Scroll the screen up by one line */
static void vga_scroll(void)
{
    /* Move every row up by one */
    for (int row = 1; row < VGA_HEIGHT; row++)
        for (int col = 0; col < VGA_WIDTH; col++)
            VGA_BUFFER[(row - 1) * VGA_WIDTH + col] =
                VGA_BUFFER[row * VGA_WIDTH + col];

    /* Clear the last row */
    uint16_t blank = vga_entry(' ', vga_color);
    for (int col = 0; col < VGA_WIDTH; col++)
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = blank;

    if (vga_row > 0)
        vga_row--;
}

/* =========================================================================
 * Character output
 * ========================================================================= */

void vga_putchar(char c)
{
    switch (c) {
    case '\n':
        vga_col = 0;
        vga_row++;
        break;
    case '\r':
        vga_col = 0;
        break;
    case '\b':
        if (vga_col > 0) {
            vga_col--;
            VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', vga_color);
        }
        break;
    default:
        VGA_BUFFER[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color);
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
        break;
    }

    if (vga_row >= VGA_HEIGHT)
        vga_scroll();

    vga_set_cursor(vga_col, vga_row);
}

/* =========================================================================
 * Driver callbacks
 * ========================================================================= */

static char vga_read(int scnd_id)
{
    (void)scnd_id;
    return 0;
}

static int vga_write(int scnd_id, char c)
{
    (void)scnd_id;
    vga_putchar(c);
    return 0;
}

/* =========================================================================
 * Initialisation – hardware + driver registration + devfs node
 * ========================================================================= */

void vga_init(void)
{
    vga_color = vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();

    char_ops_t ops = { .read = vga_read, .write = vga_write, .ioctl = NULL };
    register_char_device(0, &ops);
    devfs_register_device("vga0", DT_CHRDEV, 0, 0);
}