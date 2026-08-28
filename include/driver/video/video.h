#ifndef VIDEO_VIDEO_H
#define VIDEO_VIDEO_H

/* =========================================================================
 * Video subsystem initialisation
 * ========================================================================= */

/* Probe for a VBE-capable display controller, switch it to a high-resolution
 * graphics mode and hand the resulting framebuffer to the graphics console.
 * On any failure the system silently stays in the 80x25 VGA text mode.
 *
 * Must be called after mm_init()/vmalloc_init() (ioremap must work) and after
 * tty_init() so the graphics console can replace the text backend. */
void video_init(void);

#endif /* VIDEO_VIDEO_H */
