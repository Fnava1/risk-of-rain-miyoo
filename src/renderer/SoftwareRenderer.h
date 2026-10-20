#ifndef SOFTWARE_RENDERER_H
#define SOFTWARE_RENDERER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Scalar reference implementation for RGB565 alpha blending.
 * Formula: Dest = ((Src * Alpha) + (Dest * (256 - Alpha))) >> 8
 */
void blend_span_rgb565_scalar(uint16_t *dst, const uint16_t *src, int count, uint8_t alpha);

/**
 * ARM NEON SIMD vectorized implementation for RGB565 alpha blending.
 * Processes 8 pixels simultaneously using 128-bit vector registers.
 */
void blend_span_rgb565_neon(uint16_t *dst, const uint16_t *src, int count, uint8_t alpha);

/**
 * Fast AABB noclip culling check for bounding box rejection.
 * Returns true if sprite is entirely outside the active 640x360 viewport.
 */
bool is_sprite_culled_noclip(int x, int y, int width, int height, int view_w, int view_h);

#ifdef __cplusplus
}
#endif

#endif // SOFTWARE_RENDERER_H
