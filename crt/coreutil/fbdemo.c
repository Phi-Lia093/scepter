/* ============================================================================
 * fbdemo - exercise /dev/fb0 (framebuffer mmap) and /dev/mouse
 *
 * Draws a gradient/checker background through an mmap'd framebuffer, then
 * moves a white cursor box that follows the PS/2 mouse.  Exit with 'q' or
 * Ctrl-C.
 * ============================================================================ */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "fcntl.h"
#include "unistd.h"
#include "sys/ioctl.h"
#include "sys/mman.h"
#include "sys/fb.h"

#define CURSOR_SIZE 24

static unsigned int scr_w, scr_h, line_len;
static unsigned char *vmem;

static void draw_px(int x, int y, unsigned char r,
                    unsigned char g, unsigned char b)
{
    if (x < 0 || y < 0 || x >= (int)scr_w || y >= (int)scr_h)
        return;
    unsigned char *p = vmem + (unsigned int)y * line_len
                            + (unsigned int)x * 4;
    p[0] = b;
    p[1] = g;
    p[2] = r;
    p[3] = 0;
}

/* Deterministic background: horizontal hue ramp, vertical brightness ramp,
 * and a coarse checkerboard, so cursor movement is clearly visible. */
static void draw_background_rect(int x0, int y0, int w, int h)
{
    int x1 = x0 + w, y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)scr_w) x1 = (int)scr_w;
    if (y1 > (int)scr_h) y1 = (int)scr_h;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            unsigned char r = (unsigned char)((unsigned)x * 255U / scr_w);
            unsigned char g = (unsigned char)((unsigned)y * 255U / scr_h);
            unsigned char b = (unsigned char)((unsigned)(x + y) & 0xFF);
            if ((((x >> 5) + (y >> 5)) & 1))
                r >>= 1, g >>= 1, b >>= 1;
            draw_px(x, y, r, g, b);
        }
    }
}

static void draw_background(void)
{
    draw_background_rect(0, 0, (int)scr_w, (int)scr_h);
}

/* White outline box (cursor). */
static void draw_cursor(int x, int y)
{
    for (int i = 0; i < CURSOR_SIZE; i++) {
        draw_px(x + i, y, 255, 255, 255);
        draw_px(x + i, y + CURSOR_SIZE - 1, 255, 255, 255);
        draw_px(x, y + i, 255, 255, 255);
        draw_px(x + CURSOR_SIZE - 1, y + i, 255, 255, 255);
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("fbdemo: opening /dev/fb0...\n");

    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0) {
        printf("fbdemo: cannot open /dev/fb0\n");
        return 1;
    }

    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(fb, FBIOGET_VSCREENINFO, (unsigned int)&v) < 0 ||
        ioctl(fb, FBIOGET_FSCREENINFO, (unsigned int)&f) < 0) {
        printf("fbdemo: fb ioctl failed\n");
        return 1;
    }

    if (v.bits_per_pixel != 32) {
        printf("fbdemo: only 32bpp supported (got %u)\n", v.bits_per_pixel);
        return 1;
    }

    vmem = (unsigned char *)mmap(0, f.smem_len, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fb, 0);
    if (vmem == (unsigned char *)MAP_FAILED) {
        printf("fbdemo: mmap /dev/fb0 failed\n");
        return 1;
    }

    scr_w    = v.xres;
    scr_h    = v.yres;
    line_len = f.line_length;

    printf("fbdemo: framebuffer %ux%u %ubpp mapped at %p (len %lu)\n",
           v.xres, v.yres, v.bits_per_pixel, vmem, f.smem_len);

    int mfd = open("/dev/mouse", O_RDONLY | O_NONBLOCK);
    if (mfd < 0) {
        printf("fbdemo: cannot open /dev/mouse\n");
        return 1;
    }

    draw_background();

    int mx = (int)scr_w / 2;
    int my = (int)scr_h / 2;
    int oldx = mx, oldy = my;
    int moved = 1;

    printf("fbdemo: move the mouse (Ctrl-C to quit)\n");

    unsigned char pkt[3];
    int plen = 0;

    for (;;) {
        /* Drain all pending mouse packets. */
        int got;
        do {
            got = (int)read(mfd, pkt + plen, 3 - (unsigned int)plen);
            if (got > 0)
                plen += got;
            if (plen == 3) {
                if (pkt[0] & 0x08) {   /* valid packet start */
                    int dx = (int)pkt[1];
                    int dy = (int)pkt[2];
                    if (pkt[0] & 0x10) dx -= 256;
                    if (pkt[0] & 0x20) dy -= 256;
                    mx += dx;
                    my += dy;
                    if (mx < 0) mx = 0;
                    if (mx > (int)scr_w - CURSOR_SIZE)
                        mx = (int)scr_w - CURSOR_SIZE;
                    if (my < 0) my = 0;
                    if (my > (int)scr_h - CURSOR_SIZE)
                        my = (int)scr_h - CURSOR_SIZE;
                    moved = 1;
                }
                plen = 0;
            }
        } while (got > 0);

        if (moved) {
            /* Erase the old cursor (deterministic background redraw) and
             * draw the new one. */
            draw_background_rect(oldx - 2, oldy - 2,
                                 CURSOR_SIZE + 4, CURSOR_SIZE + 4);
            draw_cursor(mx, my);
            oldx = mx;
            oldy = my;
            moved = 0;
        }
    }

    return 0;
}
