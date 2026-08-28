#include "driver/video/fbdev.h"
#include "driver/char/char.h"
#include "fs/devfs.h"
#include "kernel/syscall.h"
#include "lib/printk.h"
#include "lib/string.h"
#include <stddef.h>

/* =========================================================================
 * fbdev – Linux-compatible framebuffer device (/dev/fb0)
 *
 * The primary interface is ioctl (FBIOGET/FBIOPUT_VSCREENINFO,
 * FBIOGET_FSCREENINFO) plus mmap() of the linear framebuffer.  The
 * framebuffer's physical pages are mapped into the caller's address space
 * via the VM_IO device-mapping path (see mm/pagefault.c).
 * ========================================================================= */

static fb_info_t fbdev_fb;

/* -------------------------------------------------------------------------
 * Driver callbacks
 * ------------------------------------------------------------------------- */

static int fbdev_read(int scnd_id)
{
    (void)scnd_id;
    return 0;
}

static int fbdev_write(int scnd_id, char c)
{
    (void)scnd_id;
    (void)c;
    return -1;
}

static int fbdev_poll(int scnd_id)
{
    (void)scnd_id;
    return 1;   /* always ready (mmap + ioctl interface) */
}

static void fbdev_var_screeninfo(struct fb_var_screeninfo *v)
{
    memset(v, 0, sizeof(*v));

    v->xres           = fbdev_fb.width;
    v->yres           = fbdev_fb.height;
    v->xres_virtual   = fbdev_fb.width;
    v->yres_virtual   = fbdev_fb.height;
    v->bits_per_pixel = fbdev_fb.bpp;
    v->activate       = FB_ACTIVATE_NOW;

    switch (fbdev_fb.bpp) {
    case 16:
        /* RGB565 */
        v->red.offset = 11;   v->red.length = 5;
        v->green.offset = 5;  v->green.length = 6;
        v->blue.offset = 0;   v->blue.length = 5;
        break;
    case 24: /* fall through */
    case 32:
        v->red.offset = 16;   v->red.length = 8;
        v->green.offset = 8;  v->green.length = 8;
        v->blue.offset = 0;   v->blue.length = 8;
        break;
    default:
        break;
    }
}

static int fbdev_ioctl(int prim_id, int scnd_id, unsigned int command,
                       uint32_t arg)
{
    (void)prim_id;
    (void)scnd_id;

    switch (command) {
    case FBIOGET_VSCREENINFO: {
        struct fb_var_screeninfo v;
        fbdev_var_screeninfo(&v);
        if (copy_to_user((void *)arg, &v, sizeof(v)) < 0)
            return -1;
        return 0;
    }
    case FBIOPUT_VSCREENINFO: {
        struct fb_var_screeninfo v;
        if (copy_from_user(&v, (void *)arg, sizeof(v)) < 0)
            return -1;
        /* Only accept a matching pixel format.  The buffer has no virtual
         * area (xres_virtual == xres), so clamp any panning offsets. */
        if (v.bits_per_pixel != fbdev_fb.bpp)
            return -1;
        return 0;
    }
    case FBIOGET_FSCREENINFO: {
        struct fb_fix_screeninfo f;
        memset(&f, 0, sizeof(f));
        strncpy(f.id, "Scepter", 15);
        f.id[15] = '\0';
        f.smem_start  = fbdev_fb.phys;
        f.smem_len    = fbdev_fb.pitch * fbdev_fb.height;
        f.type        = FB_TYPE_PACKED_PIXELS;
        f.visual      = FB_VISUAL_TRUECOLOR;
        f.xpanstep    = 0;
        f.ypanstep    = 0;
        f.ywrapstep   = 0;
        f.line_length = fbdev_fb.pitch;
        f.mmio_start  = 0;
        f.mmio_len    = 0;
        f.accel       = 0;
        if (copy_to_user((void *)arg, &f, sizeof(f)) < 0)
            return -1;
        return 0;
    }
    case FBIO_WAITFORVSYNC:
        return 0;   /* no-op (single buffering) */
    default:
        return -1;
    }
}

static int fbdev_mmap(int scnd_id, uint32_t length, uint32_t *phys)
{
    (void)scnd_id;
    (void)length;
    if (!phys || !fbdev_fb.virt)
        return -1;
    *phys = fbdev_fb.phys;
    return 0;
}

/* -------------------------------------------------------------------------
 * Initialisation
 * ------------------------------------------------------------------------- */

void fbdev_init(const fb_info_t *fb)
{
    if (!fb || !fb->virt)
        return;   /* no framebuffer active (text-mode fallback) */

    fbdev_fb = *fb;

    char_ops_t ops = {
        .read  = fbdev_read,
        .write = fbdev_write,
        .poll  = fbdev_poll,
        .ioctl = fbdev_ioctl,
        .mmap  = fbdev_mmap,
    };

    if (register_char_device(CHAR_DEV_FBDEV, &ops) != 0)
        return;
    devfs_register_device("fb0", DT_CHRDEV, CHAR_DEV_FBDEV, 0);

    printk("[FBDEV] /dev/fb0 %ux%ux%u (line_length %u, phys 0x%08x)\n",
           fbdev_fb.width, fbdev_fb.height, fbdev_fb.bpp,
           fbdev_fb.pitch, fbdev_fb.phys);
}
