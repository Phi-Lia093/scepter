#ifndef VIDEO_VBE_H
#define VIDEO_VBE_H

#include <stdint.h>

/* =========================================================================
 * VBE (VESA BIOS Extensions) display driver
 *
 * Programs the standard "VBE/embedded" (DISPI) interface that VBE-capable
 * display controllers expose through I/O ports 0x01CE/0x01CF.  The interface
 * is detected generically by reading the DISPI ID register, and the linear
 * framebuffer address is taken from the device's PCI memory BAR -- no
 * emulator-specific assumptions are made.  If no such controller exists the
 * kernel simply stays in the 80x25 VGA text mode.
 * ========================================================================= */

/* DISPI I/O ports */
#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

/* DISPI register indices */
#define VBE_DISPI_INDEX_ID          0x0
#define VBE_DISPI_INDEX_XRES        0x1
#define VBE_DISPI_INDEX_YRES        0x2
#define VBE_DISPI_INDEX_BPP         0x3
#define VBE_DISPI_INDEX_ENABLE      0x4
#define VBE_DISPI_INDEX_BANK        0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9

/* DISPI enable flags */
#define VBE_DISPI_DISABLED      0x00
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_GETCAPS       0x02
#define VBE_DISPI_8BIT_DAC      0x20
#define VBE_DISPI_LFB_ENABLED   0x40
#define VBE_DISPI_NO_CLEARMEM   0x80

/* Known DISPI interface IDs */
#define VBE_DISPI_ID_BOCHS      0xB0C4
#define VBE_DISPI_ID_QEMU       0xB0C5

/* Result of probing for a VBE-capable display controller. */
typedef struct {
    int      found;          /* 1 if a usable display was located          */
    uint8_t  bus, slot, func;   /* PCI location of the display device      */
    uint32_t lfb_phys;       /* physical address of the linear framebuffer */
    uint32_t lfb_size;       /* size in bytes of the LFB window (PCI BAR)  */
} vbe_display_t;

/* Scan PCI for a display controller that implements the VBE DISPI interface.
 * Returns 1 and fills *disp on success, 0 if none was found. */
int vbe_find_display(vbe_display_t *disp);

/* Return 1 if a VBE DISPI interface is present on the bus. */
int vbe_available(void);

/* Try to switch the display to xres*yres*bpp.  Returns 0 on success (the
 * actual mode is reported back through out_w/out_h/out_bpp -- a controller
 * may clamp to a supported mode), or -1 on failure. */
int vbe_set_mode(int xres, int yres, int bpp,
                 int *out_w, int *out_h, int *out_bpp);

/* Switch the display back to the legacy text mode (no-op on failure paths
 * where the display was never enabled). */
void vbe_disable(void);

#endif /* VIDEO_VBE_H */
