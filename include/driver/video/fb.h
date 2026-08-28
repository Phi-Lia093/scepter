#ifndef VIDEO_FB_H
#define VIDEO_FB_H

#include <stdint.h>

/* =========================================================================
 * Framebuffer abstraction
 *
 * A generic linear-framebuffer description used by the VBE driver and the
 * graphics console.  Covers the formats a VBE DISPI controller can expose:
 * 32bpp (8:8:8 XRGB), 24bpp (BGR byte order) and 16bpp (RGB565).
 * ========================================================================= */

#define FB_TYPE_RGB 1

typedef struct {
    uint32_t phys;          /* physical address of the framebuffer    */
    uint8_t *virt;          /* kernel virtual address (ioremap'd)     */
    uint32_t width;         /* visible width in pixels               */
    uint32_t height;        /* visible height in pixels              */
    uint32_t pitch;         /* bytes per scanline                    */
    uint8_t  bpp;           /* bits per pixel (16/24/32)             */
    uint8_t  type;          /* FB_TYPE_*                             */
    uint32_t r_mask, g_mask, b_mask;   /* colour masks (informational) */
} fb_info_t;

/* Draw one pixel with a 24-bit RGB colour (0xRRGGBB). */
void fb_put_pixel(const fb_info_t *fb, int x, int y, uint32_t rgb);

/* Fill a rectangle with a 24-bit RGB colour. */
void fb_fill_rect(const fb_info_t *fb, int x, int y, int w, int h, uint32_t rgb);

/* Scroll the framebuffer up by `line_pixels` rows (content above the top is
 * lost; the vacated bottom strip is zero-filled). */
void fb_scroll_lines(const fb_info_t *fb, int line_pixels);

#endif /* VIDEO_FB_H */
