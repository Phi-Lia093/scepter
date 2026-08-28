#ifndef VIDEO_GFXCON_H
#define VIDEO_GFXCON_H

#include <stdint.h>
#include "driver/video/fb.h"
#include "driver/char/tty.h"

/* =========================================================================
 * Graphics console
 *
 * A text-mode style console rendered on a linear framebuffer: keeps a cell
 * buffer (character + 16-colour attribute) and blits 8x16 glyphs to the
 * framebuffer.  Doubles as the kernel console (printk) and as the tty's
 * output backend.
 * ========================================================================= */

/* Initialise the console on the given framebuffer.  Must be called after fb
 * is mapped and before any other gfxcon function. */
void gfxcon_init(const fb_info_t *fb);

/* Kernel-console style single character output (used by printk). */
void gfxcon_putchar(char c);

/* Fill the tty output backend struct for this console. */
void gfxcon_fill_tty_backend(tty_backend_t *be);

/* Copy the contents of a legacy VGA text-mode buffer (16-bit cells) into the
 * top-left of the graphics console and place the cursor at (cur_col,cur_row). */
void gfxcon_import_textbuf(const uint16_t *buf, int w, int h,
                           int cur_col, int cur_row);

/* Query the current text grid dimensions. */
void gfxcon_get_size(int *cols, int *rows);

#endif /* VIDEO_GFXCON_H */
