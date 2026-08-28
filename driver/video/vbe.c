#include "driver/video/vbe.h"
#include "driver/pci/pci.h"
#include "kernel/asm.h"
#include "lib/printk.h"
#include "lib/string.h"

/* -------------------------------------------------------------------------
 * DISPI port access
 * ------------------------------------------------------------------------- */

static void vbe_write(uint16_t index, uint16_t value)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t vbe_read(uint16_t index)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

int vbe_available(void)
{
    uint16_t id = vbe_read(VBE_DISPI_INDEX_ID);
    return (id == VBE_DISPI_ID_BOCHS || id == VBE_DISPI_ID_QEMU);
}

/* -------------------------------------------------------------------------
 * PCI display discovery
 * ------------------------------------------------------------------------- */

static vbe_display_t vbe_found;

/* Size of a PCI memory BAR (standard sizing protocol: write all-ones, read
 * back the decode mask, restore).  Returns 0 for I/O BARs / zero-sized. */
static uint32_t pci_bar_size(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t orig = pci_config_read_dword(bus, slot, func, offset);
    pci_config_write_dword(bus, slot, func, offset, 0xFFFFFFFF);
    uint32_t mask = pci_config_read_dword(bus, slot, func, offset);
    pci_config_write_dword(bus, slot, func, offset, orig);

    if (mask & PCI_BAR_IO)
        return 0;                       /* not a memory BAR */
    return ~(mask & 0xFFFFFFF0) + 1;
}

static void vbe_scan_callback(pci_device_t *device)
{
    if (vbe_found.found)
        return;

    /* Pick the largest memory BAR -- that is the linear framebuffer window. */
    uint32_t best_size = 0;
    for (int i = 0; i < 6; i++) {
        if (device->bar[i] & PCI_BAR_IO)
            continue;
        uint32_t size = pci_bar_size(device->bus, device->slot, device->func,
                                     PCI_REG_BAR0 + (uint8_t)(i * 4));
        uint32_t base = device->bar[i] & 0xFFFFFFF0;
        if (base == 0 || size == 0 || size < 64 * 1024)
            continue;
        if (size > best_size) {
            best_size = size;
            vbe_found.lfb_phys = base;
            vbe_found.lfb_size = size;
        }
    }
    if (best_size == 0)
        return;

    vbe_found.bus   = device->bus;
    vbe_found.slot  = device->slot;
    vbe_found.func  = device->func;
    vbe_found.found = 1;
}

int vbe_find_display(vbe_display_t *disp)
{
    memset(&vbe_found, 0, sizeof(vbe_found));

    if (!vbe_available())
        return 0;

    pci_scan_devices(PCI_CLASS_DISPLAY, 0xFF, vbe_scan_callback);

    if (disp)
        *disp = vbe_found;
    return vbe_found.found;
}

/* -------------------------------------------------------------------------
 * Mode setting
 * ------------------------------------------------------------------------- */

int vbe_set_mode(int xres, int yres, int bpp, int *out_w, int *out_h, int *out_bpp)
{
    /* Disable while reprogramming. */
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    vbe_write(VBE_DISPI_INDEX_XRES, (uint16_t)xres);
    vbe_write(VBE_DISPI_INDEX_YRES, (uint16_t)yres);
    vbe_write(VBE_DISPI_INDEX_BPP, (uint16_t)bpp);
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    /* The controller may clamp to a supported mode; read back the truth. */
    uint16_t rx = vbe_read(VBE_DISPI_INDEX_XRES);
    uint16_t ry = vbe_read(VBE_DISPI_INDEX_YRES);
    uint16_t rb = vbe_read(VBE_DISPI_INDEX_BPP);

    if (rx == 0 || ry == 0 || (rb != 4 && rb != 8 && rb != 15 &&
                               rb != 16 && rb != 24 && rb != 32)) {
        vbe_disable();
        return -1;
    }

    if (out_w)   *out_w   = rx;
    if (out_h)   *out_h   = ry;
    if (out_bpp) *out_bpp = rb;
    return 0;
}

void vbe_disable(void)
{
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
}
