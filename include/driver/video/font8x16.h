#ifndef VIDEO_FONT8X16_H
#define VIDEO_FONT8X16_H

#include <stdint.h>

/* Classic VGA 8x16 ROM font, glyphs 0x00-0x7F (public domain).
 * Each glyph is 16 scanline bytes; bit 7 (MSB) = leftmost pixel. */
extern const uint8_t font8x16[128][16];

#endif /* VIDEO_FONT8X16_H */
