#ifndef MMP_VIDEO_H
#define MMP_VIDEO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Paces frames accurately on Linux kernels with CONFIG_HZ=100.
 * Sleeps via nanosleep only when remaining time exceeds 12ms to avoid scheduler quantum overshoot,
 * then yields via sched_yield() during the final millisecond window.
 */
void mmp_frame_pacing(uint64_t target_frame_time_ns);

/**
 * Calculates rotated physical coordinates for Miyoo Mini's 180-degree inverted display panel.
 */
void mmp_invert_coords_180(int x, int y, int width, int height, int *out_x, int *out_y);

/**
 * Initializes direct framebuffer access (/dev/fb0).
 */
int mmp_fb_init(const char *fb_device);

/**
 * Closes and unmaps framebuffer.
 */
void mmp_fb_close(void);

/**
 * Blits a 16-bit RGB565 surface directly to framebuffer with 180-degree rotation.
 */
void mmp_video_flip_180(const uint16_t *src_buffer, int width, int height);

#ifdef __cplusplus
}
#endif

#endif // MMP_VIDEO_H
