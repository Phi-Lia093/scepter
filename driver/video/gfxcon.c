#include "driver/video/gfxcon.h"
#include "driver/video/fb.h"
#include "driver/video/font8x16.h"
#include "driver/char/tty.h"
#include <stddef.h>

/* =========================================================================
 * Graphics console
 *
 * Keeps a text-mode style cell buffer (character + 16-colour attribute) and
 * blits 8x16 glyphs to a linear framebuffer.  Serves both as the kernel
 * console (printk via gfxcon_putchar) and as the tty output backend.
 * ========================================================================= */

#define GFXCON_MAX_COLS 256
#define GFXCON_MAX_ROWS 128
#define FONT_W 8
#define FONT_H 16

/* VGA 16-colour palette -> 24-bit RGB. */
static const uint32_t gfxcon_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

typedef struct {
    uint8_t ch;
    uint8_t fg;
    uint8_t bg;
} gfxcon_cell_t;

static struct {
    const fb_info_t *fb;
    int cols;
    int rows;
    gfxcon_cell_t cells[GFXCON_MAX_ROWS][GFXCON_MAX_COLS];
    int cur_col;        /* cursor column (in cells) */
    int cur_row;        /* cursor row (in cells)    */
    int cursor_on;
} gfxcon;

/* Render one cell; the software cursor (inverted colours) is drawn when the
 * cell is the current cursor position. */
static void gfxcon_render_cell(int col, int row)
{
    const gfxcon_cell_t *cell = &gfxcon.cells[row][col];
    int x0 = col * FONT_W;
    int y0 = row * FONT_H;
    const uint8_t *glyph = font8x16[cell->ch & 0x7F];
    int show_cursor = gfxcon.cursor_on && gfxcon.cur_col == col &&
                      gfxcon.cur_row == row;

    uint32_t fg = gfxcon_palette[cell->fg & 0x0F];
    uint32_t bg = gfxcon_palette[cell->bg & 0x0F];
    if (show_cursor) {
        uint32_t tmp = fg; fg = bg; bg = tmp;
    }

    for (int y = 0; y < FONT_H; y++) {
        uint8_t line = glyph[y];
        for (int x = 0; x < FONT_W; x++)
            fb_put_pixel(gfxcon.fb, x0 + x, y0 + y,
                         (line & (0x80 >> x)) ? fg : bg);
    }
}

/* -------------------------------------------------------------------------
 * tty backend callbacks
 * ------------------------------------------------------------------------- */

static void gfxcon_backend_write_cell(int col, int row, char c, uint8_t fg, uint8_t bg)
{
    if (col < 0 || col >= gfxcon.cols || row < 0 || row >= gfxcon.rows)
        return;
    gfxcon.cells[row][col].ch = (uint8_t)c;
    gfxcon.cells[row][col].fg = fg;
    gfxcon.cells[row][col].bg = bg;
    gfxcon_render_cell(col, row);
}


static void gfxcon_backend_clear(void)
{
    for (int r = 0; r < gfxcon.rows; r++) {
        for (int c = 0; c < gfxcon.cols; c++) {
            gfxcon.cells[r][c].ch = ' ';
            gfxcon.cells[r][c].fg = 7;   /* light grey on black */
            gfxcon.cells[r][c].bg = 0;
        }
    }
    fb_fill_rect(gfxcon.fb, 0, 0, (int)gfxcon.fb->width, (int)gfxcon.fb->height, 0);
    gfxcon.cur_col = 0;
    gfxcon.cur_row = 0;
    gfxcon.cursor_on = 0;
}

/* Scroll the console up one line.  The cursor is erased first (so no stale
 * inverted block survives the pixel shift) and its position follows the
 * content that moved up.  The caller re-draws it afterwards. */
static void gfxcon_backend_scroll(void)
{
    if (gfxcon.cursor_on) {
        gfxcon.cursor_on = 0;
        gfxcon_render_cell(gfxcon.cur_col, gfxcon.cur_row);
    }

    for (int r = 1; r < gfxcon.rows; r++)
        for (int c = 0; c < gfxcon.cols; c++)
            gfxcon.cells[r - 1][c] = gfxcon.cells[r][c];

    for (int c = 0; c < gfxcon.cols; c++) {
        gfxcon.cells[gfxcon.rows - 1][c].ch = ' ';
        gfxcon.cells[gfxcon.rows - 1][c].fg = 7;
        gfxcon.cells[gfxcon.rows - 1][c].bg = 0;
    }

    fb_scroll_lines(gfxcon.fb, FONT_H);
    for (int c = 0; c < gfxcon.cols; c++)
        gfxcon_render_cell(c, gfxcon.rows - 1);

    if (gfxcon.cur_row > 0)
        gfxcon.cur_row--;
}

static void gfxcon_backend_set_cursor(int col, int row)
{
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col >= gfxcon.cols) col = gfxcon.cols - 1;
    if (row >= gfxcon.rows) row = gfxcon.rows - 1;

    if (gfxcon.cursor_on) {
        gfxcon.cursor_on = 0;
        gfxcon_render_cell(gfxcon.cur_col, gfxcon.cur_row);
    }
    gfxcon.cur_col = col;
    gfxcon.cur_row = row;
    gfxcon.cursor_on = 1;
    gfxcon_render_cell(col, row);
}

static void gfxcon_redraw_cursor(void)
{
    gfxcon_backend_set_cursor(gfxcon.cur_col, gfxcon.cur_row);
}


/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void gfxcon_init(const fb_info_t *fb)
{
    gfxcon.fb = fb;
    gfxcon.cols = (int)(fb->width / FONT_W);
    gfxcon.rows = (int)(fb->height / FONT_H);
    if (gfxcon.cols > GFXCON_MAX_COLS) gfxcon.cols = GFXCON_MAX_COLS;
    if (gfxcon.rows > GFXCON_MAX_ROWS) gfxcon.rows = GFXCON_MAX_ROWS;

    gfxcon_backend_clear();
    gfxcon.cursor_on = 1;
    gfxcon_render_cell(0, 0);
}

void gfxcon_putchar(char c)
{
    if (!gfxcon.fb)
        return;

    switch (c) {
    case '\n':
        gfxcon.cur_col = 0;
        if (++gfxcon.cur_row >= gfxcon.rows) {
            gfxcon_backend_scroll();
            gfxcon.cur_row = gfxcon.rows - 1;
        }
        break;
    case '\r':
        gfxcon.cur_col = 0;
        break;
    case '\b':
        if (gfxcon.cur_col > 0) {
            gfxcon.cur_col--;
            gfxcon_backend_write_cell(gfxcon.cur_col, gfxcon.cur_row, ' ', 7, 0);
        }
        break;
    case '\t':
        {
            int next = ((gfxcon.cur_col / 8) + 1) * 8;
            while (gfxcon.cur_col < next && gfxcon.cur_col < gfxcon.cols) {
                gfxcon_backend_write_cell(gfxcon.cur_col, gfxcon.cur_row, ' ', 7, 0);
                gfxcon.cur_col++;
            }
        }
        break;
    default:
        if (c >= 32 && c <= 126) {
            gfxcon_backend_write_cell(gfxcon.cur_col, gfxcon.cur_row, c, 7, 0);
            if (++gfxcon.cur_col >= gfxcon.cols) {
                gfxcon.cur_col = 0;
                if (++gfxcon.cur_row >= gfxcon.rows) {
                    gfxcon_backend_scroll();
                    gfxcon.cur_row = gfxcon.rows - 1;
                }
            }
        }
        break;
    }

    gfxcon_redraw_cursor();
}

void gfxcon_fill_tty_backend(tty_backend_t *be)
{
    be->cols = gfxcon.cols;
    be->rows = gfxcon.rows;
    be->clear = gfxcon_backend_clear;
    be->scroll = gfxcon_backend_scroll;
    be->write_cell = gfxcon_backend_write_cell;
    be->set_cursor = gfxcon_backend_set_cursor;
}

void gfxcon_import_textbuf(const uint16_t *buf, int w, int h,
                           int cur_col, int cur_row)
{
    if (!gfxcon.fb || !buf)
        return;

    int maxw = (w < gfxcon.cols) ? w : gfxcon.cols;
    int maxh = (h < gfxcon.rows) ? h : gfxcon.rows;

    gfxcon.cursor_on = 0;
    for (int r = 0; r < maxh; r++) {
        for (int c = 0; c < maxw; c++) {
            uint16_t cell = buf[r * w + c];
            gfxcon.cells[r][c].ch = (uint8_t)(cell & 0xFF);
            gfxcon.cells[r][c].fg = (uint8_t)((cell >> 8) & 0x0F);
            gfxcon.cells[r][c].bg = (uint8_t)((cell >> 12) & 0x0F);
        }
    }
    for (int r = 0; r < gfxcon.rows; r++)
        for (int c = 0; c < gfxcon.cols; c++)
            gfxcon_render_cell(c, r);

    if (cur_col < 0) cur_col = 0;
    if (cur_row < 0) cur_row = 0;
    if (cur_col >= gfxcon.cols) cur_col = gfxcon.cols - 1;
    if (cur_row >= gfxcon.rows) cur_row = gfxcon.rows - 1;
    gfxcon.cur_col = cur_col;
    gfxcon.cur_row = cur_row;
    gfxcon.cursor_on = 1;
    gfxcon_render_cell(cur_col, cur_row);
}

void gfxcon_get_size(int *cols, int *rows)
{
    if (cols) *cols = gfxcon.cols;
    if (rows) *rows = gfxcon.rows;
}

