#include "driver/video/video.h"
#include "driver/video/vbe.h"
#include "driver/video/fb.h"
#include "driver/video/gfxcon.h"
#include "driver/char/tty.h"
#include "driver/char/vga.h"
#include "driver/pci/pci.h"
#include "mm/vmalloc.h"
#include "lib/printk.h"
#include <stddef.h>

/* Snapshot of the 80x25 text screen taken before leaving text mode, so the
 * boot log can be carried over to the graphics console. */
static uint16_t boot_text_snapshot[80 * 25];

/* The framebuffer description stays valid for the lifetime of the kernel:
 * the graphics console keeps a pointer to it. */
static fb_info_t fb;

void video_init(void)
{
    vbe_display_t disp;
    int w = 0, h = 0, bpp = 0;

    if (!vbe_find_display(&disp)) {
        printk("[VIDEO] No VBE-capable display controller found - "
               "keeping 80x25 text mode\n");
        return;
    }

    /* Snapshot the boot log and hardware cursor while still in text mode. */
    volatile uint16_t *text = (volatile uint16_t *)0xC00B8000;
    for (int i = 0; i < 80 * 25; i++)
        boot_text_snapshot[i] = text[i];
    uint8_t tcol = 0, trow = 0;
    vga_get_cursor(&tcol, &trow);

    /* Preferred modes, in order of desirability.  The controller is
     * authoritative: it may clamp to a supported mode (reported via
     * out_w/out_h/out_bpp), which we then use as-is. */
    static const struct { int w, h, bpp; } modes[] = {
        { 1024, 768, 32 }, { 800, 600, 32 }, { 640, 480, 32 },
        { 1024, 768, 16 }, { 800, 600, 16 }, { 640, 480, 16 },
    };

    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (vbe_set_mode(modes[i].w, modes[i].h, modes[i].bpp,
                         &w, &h, &bpp) == 0)
            break;
        w = h = bpp = 0;
    }

    if (w == 0 || h == 0 || (bpp != 16 && bpp != 24 && bpp != 32)) {
        printk("[VIDEO] VBE mode set failed - keeping 80x25 text mode\n");
        return;
    }

    /* Enable PCI memory space so the linear framebuffer is decodable. */
    pci_enable_device(disp.bus, disp.slot, disp.func, PCI_COMMAND_MEMORY);

    /* The framebuffer lives in high physical memory, beyond the direct map,
     * so it must be ioremapped. */
    uint32_t pitch = (uint32_t)w * (uint32_t)(bpp / 8);
    uint32_t fb_bytes = pitch * (uint32_t)h;
    uint8_t *virt = ioremap(disp.lfb_phys, fb_bytes);
    if (!virt) {
        printk("[VIDEO] Failed to ioremap framebuffer 0x%08x\n", disp.lfb_phys);
        return;
    }

    fb.phys   = disp.lfb_phys;
    fb.virt   = virt;
    fb.width  = (uint32_t)w;
    fb.height = (uint32_t)h;
    fb.pitch  = pitch;
    fb.bpp    = (uint8_t)bpp;
    fb.type   = FB_TYPE_RGB;
    if (bpp == 16) {
        fb.r_mask = 0xF800; fb.g_mask = 0x07E0; fb.b_mask = 0x001F;
    } else {
        fb.r_mask = 0xFF0000; fb.g_mask = 0x00FF00; fb.b_mask = 0x0000FF;
    }

    gfxcon_init(&fb);

    /* Carry the boot log over, then point the kernel console and the tty at
     * the graphics console. */
    gfxcon_import_textbuf(boot_text_snapshot, 80, 25, tcol, trow);

    tty_backend_t be;
    gfxcon_fill_tty_backend(&be);
    tty_attach_backend(&be);

    console_set_putchar(gfxcon_putchar);

    printk("[VIDEO] VBE %dx%dx%d, LFB phys 0x%08x -> virt 0x%08x\n",
           w, h, bpp, disp.lfb_phys, (uint32_t)virt);
}
