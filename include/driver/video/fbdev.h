#ifndef VIDEO_FBDEV_H
#define VIDEO_FBDEV_H

#include <stdint.h>

/* =========================================================================
 * Linux-compatible fbdev interface for /dev/fb0.
 *
 * The ioctl numbers and structure layouts below match the Linux 32-bit
 * ABI (linux/fb.h) so userland programs written against Linux fbdev work
 * unchanged.  The userland mirror of this header is crt/include/sys/fb.h.
 * ========================================================================= */

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIO_WAITFORVSYNC   0x4620

#include "driver/video/fb.h"

#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR   2
#define FB_ACTIVATE_NOW       0

/* Color bitfield within a pixel (Linux struct fb_bitfield). */
struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

/* Variable screen information (Linux 32-bit layout, 160 bytes). */
struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
};

/* Fixed screen information (Linux 32-bit layout, 64 bytes). */
struct fb_fix_screeninfo {
    char     id[16];
    uint32_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint32_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
};

/* Register /dev/fb0 for the given active framebuffer.  Does nothing when
 * fb is NULL or the framebuffer is not mapped (80x25 text fallback). */
void fbdev_init(const fb_info_t *fb);

#endif /* VIDEO_FBDEV_H */
