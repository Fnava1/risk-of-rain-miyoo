#ifndef FB_PROGRESS_H
#define FB_PROGRESS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize framebuffer display (/dev/fb0).
 * Queries FBIOGET_VSCREENINFO and FBIOGET_FSCREENINFO, mmap's framebuffer.
 * If /dev/fb0 cannot be opened or queried, falls back to console stdout mode.
 * Returns 0 on success. */
int fb_progress_init(void);

/* Check if /dev/fb0 is actively mapped (1) or in console fallback mode (0). */
int fb_progress_is_fb_active(void);

/* Render or print progress update.
 * progress is in range [0.0, 1.0].
 * status_text is displayed below progress bar or printed to console. */
void fb_progress_update(float progress, const char *status_text);

/* Display a modal error message screen on /dev/fb0. */
void fb_progress_show_error(const char *title, const char *line1, const char *line2, const char *line3);

/* Cleans up fb memory mapping and closes fd if active. */
void fb_progress_close(void);

#ifdef __cplusplus
}
#endif

#endif /* FB_PROGRESS_H */
