#include "driver/video/fb.h"
#include <stddef.h>

/* Convert a 24-bit RGB value (0xRRGGBB) into the device's pixel format. */
static uint32_t fb_rgb_to_pixel(const fb_info_t *fb, uint32_t rgb)
{
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;

    switch (fb->bpp) {
    case 32:
        /* 8:8:8 XRGB, little-endian: byte order BB GG RR XX */
        return b | ((uint32_t)g << 8) | ((uint32_t)r << 16);
    case 16:
        /* RGB565 */
        return ((uint32_t)(r >> 3) << 11) |
               ((uint32_t)(g >> 2) << 5)  |
               ((uint32_t)(b >> 3));
    default:
        return 0;
    }
}

void fb_put_pixel(const fb_info_t *fb, int x, int y, uint32_t rgb)
{
    if (!fb || !fb->virt)
        return;
    if (x < 0 || y < 0 || x >= (int)fb->width || y >= (int)fb->height)
        return;

    uint32_t bytes = fb->bpp / 8;
    uint8_t *px = fb->virt + (uint32_t)y * fb->pitch + (uint32_t)x * bytes;

    switch (fb->bpp) {
    case 32:
        *(uint32_t *)px = fb_rgb_to_pixel(fb, rgb);
        break;
    case 24:
        px[0] = rgb & 0xFF;
        px[1] = (rgb >> 8) & 0xFF;
        px[2] = (rgb >> 16) & 0xFF;
        break;
    case 16:
        *(uint16_t *)px = (uint16_t)fb_rgb_to_pixel(fb, rgb);
        break;
    default:
        break;
    }
}

void fb_fill_rect(const fb_info_t *fb, int x, int y, int w, int h, uint32_t rgb)
{
    if (!fb || !fb->virt)
        return;

    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb->width)  w = (int)fb->width - x;
    if (y + h > (int)fb->height) h = (int)fb->height - y;
    if (w <= 0 || h <= 0)
        return;

    uint32_t bytes = fb->bpp / 8;
    uint32_t pixel = fb_rgb_to_pixel(fb, rgb);
    uint8_t *base = fb->virt + (uint32_t)y * fb->pitch + (uint32_t)x * bytes;

    for (int row = 0; row < h; row++) {
        uint8_t *line = base + (uint32_t)row * fb->pitch;
        switch (fb->bpp) {
        case 32:
            for (int col = 0; col < w; col++)
                ((uint32_t *)line)[col] = pixel;
            break;
        case 16:
            for (int col = 0; col < w; col++)
                ((uint16_t *)line)[col] = (uint16_t)pixel;
            break;
        case 24:
            for (int col = 0; col < w; col++) {
                line[col * 3 + 0] = rgb & 0xFF;
                line[col * 3 + 1] = (rgb >> 8) & 0xFF;
                line[col * 3 + 2] = (rgb >> 16) & 0xFF;
            }
            break;
        default:
            break;
        }
    }
}

void fb_scroll_lines(const fb_info_t *fb, int line_pixels)
{
    if (!fb || !fb->virt || line_pixels <= 0)
        return;
    if (line_pixels >= (int)fb->height) {
        fb_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, 0);
        return;
    }

    uint32_t keep_bytes = ((uint32_t)fb->height - (uint32_t)line_pixels) * fb->pitch;
    uint8_t *base = fb->virt;

    extern void *memmove(void *dst, const void *src, size_t n);
    extern void *memset(void *s, int c, size_t n);

    memmove(base, base + (uint32_t)line_pixels * fb->pitch, keep_bytes);
    memset(base + keep_bytes, 0, (uint32_t)line_pixels * fb->pitch);
}
