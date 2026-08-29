#ifndef _SYS_FB_H
#define _SYS_FB_H

/* =========================================================================
 * Linux-compatible fbdev interface (mirrors include/driver/video/fbdev.h)
 * ========================================================================= */

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIO_WAITFORVSYNC   0x4620

#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR   2
#define FB_ACTIVATE_NOW       0

/* Color bitfield within a pixel (Linux struct fb_bitfield). */
struct fb_bitfield {
    unsigned int offset;
    unsigned int length;
    unsigned int msb_right;
};

/* Variable screen information (Linux 32-bit layout, 160 bytes). */
struct fb_var_screeninfo {
    unsigned int xres;
    unsigned int yres;
    unsigned int xres_virtual;
    unsigned int yres_virtual;
    unsigned int xoffset;
    unsigned int yoffset;
    unsigned int bits_per_pixel;
    unsigned int grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    unsigned int nonstd;
    unsigned int activate;
    unsigned int height;
    unsigned int width;
    unsigned int accel_flags;
    unsigned int pixclock;
    unsigned int left_margin;
    unsigned int right_margin;
    unsigned int upper_margin;
    unsigned int lower_margin;
    unsigned int hsync_len;
    unsigned int vsync_len;
    unsigned int sync;
    unsigned int vmode;
    unsigned int rotate;
    unsigned int colorspace;
    unsigned int reserved[4];
};

/* Fixed screen information (Linux 32-bit layout, 64 bytes). */
struct fb_fix_screeninfo {
    char             id[16];
    uint32_t        smem_start;
    uint32_t        smem_len;
    unsigned int     type;
    unsigned int     type_aux;
    unsigned int     visual;
    unsigned short   xpanstep;
    unsigned short   ypanstep;
    unsigned short   ywrapstep;
    uint32_t        line_length;
    uint32_t        mmio_start;
    uint32_t        mmio_len;
    unsigned int     accel;
    unsigned short   capabilities;
    unsigned short   reserved[2];
};

#endif /* _SYS_FB_H */
