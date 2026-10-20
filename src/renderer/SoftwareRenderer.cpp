#include "SoftwareRenderer.h"
#include <string.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

static inline uint16_t blend_pixel_rgb565(uint16_t d, uint16_t s, uint8_t a) {
    uint32_t s_r = (s >> 11) & 0x1F;
    uint32_t s_g = (s >> 5) & 0x3F;
    uint32_t s_b = s & 0x1F;

    uint32_t d_r = (d >> 11) & 0x1F;
    uint32_t d_g = (d >> 5) & 0x3F;
    uint32_t d_b = d & 0x1F;

    uint32_t inv_a = 256 - a;
    uint32_t r = (s_r * a + d_r * inv_a) >> 8;
    uint32_t g = (s_g * a + d_g * inv_a) >> 8;
    uint32_t b = (s_b * a + d_b * inv_a) >> 8;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

void blend_span_rgb565_scalar(uint16_t *dst, const uint16_t *src, int count, uint8_t alpha) {
    if (!dst || !src || count <= 0) return;

    for (int i = 0; i < count; i++) {
        dst[i] = blend_pixel_rgb565(dst[i], src[i], alpha);
    }
}

void blend_span_rgb565_neon(uint16_t *dst, const uint16_t *src, int count, uint8_t alpha) {
    if (!dst || !src || count <= 0) return;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    int i = 0;
    uint16_t inv_alpha = 256 - alpha;

    uint16x8_t v_alpha = vdupq_n_u16(alpha);
    uint16x8_t v_inv_alpha = vdupq_n_u16(inv_alpha);
    uint16x8_t v_mask_g = vdupq_n_u16(0x3F);
    uint16x8_t v_mask_b = vdupq_n_u16(0x1F);

    // Process 8 pixels at a time with 128-bit vector registers
    for (; i <= count - 8; i += 8) {
        uint16x8_t v_src = vld1q_u16(src + i);
        uint16x8_t v_dst = vld1q_u16(dst + i);

        // Unpack channels
        uint16x8_t s_r = vshrq_n_u16(v_src, 11);
        uint16x8_t s_g = vandq_u16(vshrq_n_u16(v_src, 5), v_mask_g);
        uint16x8_t s_b = vandq_u16(v_src, v_mask_b);

        uint16x8_t d_r = vshrq_n_u16(v_dst, 11);
        uint16x8_t d_g = vandq_u16(vshrq_n_u16(v_dst, 5), v_mask_g);
        uint16x8_t d_b = vandq_u16(v_dst, v_mask_b);

        // Blend: (src * alpha + dst * inv_alpha) >> 8
        uint16x8_t r = vshrq_n_u16(vmlaq_u16(vmulq_u16(s_r, v_alpha), d_r, v_inv_alpha), 8);
        uint16x8_t g = vshrq_n_u16(vmlaq_u16(vmulq_u16(s_g, v_alpha), d_g, v_inv_alpha), 8);
        uint16x8_t b = vshrq_n_u16(vmlaq_u16(vmulq_u16(s_b, v_alpha), d_b, v_inv_alpha), 8);

        // Repack: (r << 11) | (g << 5) | b
        uint16x8_t out = vorrq_u16(vorrq_u16(vshlq_n_u16(r, 11), vshlq_n_u16(g, 5)), b);
        vst1q_u16(dst + i, out);
    }

    // Handle scalar remainder
    for (; i < count; i++) {
        dst[i] = blend_pixel_rgb565(dst[i], src[i], alpha);
    }
#else
    blend_span_rgb565_scalar(dst, src, count, alpha);
#endif
}

bool is_sprite_culled_noclip(int x, int y, int width, int height, int view_w, int view_h) {
    if (x + width <= 0) return true;
    if (y + height <= 0) return true;
    if (x >= view_w) return true;
    if (y >= view_h) return true;
    return false;
}
