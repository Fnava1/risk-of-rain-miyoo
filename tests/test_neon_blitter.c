#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "src/renderer/SoftwareRenderer.h"

int main() {
    printf("[*] Running SoftwareRenderer ARM NEON Vectorization Test...\n");

    const int count = 256;
    uint16_t src[count];
    uint16_t dst_scalar[count];
    uint16_t dst_neon[count];

    // Initialize test pattern
    for (int i = 0; i < count; i++) {
        src[i] = (uint16_t)(0xF800 | (i & 0x07E0) | (i & 0x001F)); // Red-tint pattern
        dst_scalar[i] = (uint16_t)(0x001F | ((i * 2) & 0x07E0));   // Blue-tint background
        dst_neon[i] = dst_scalar[i];
    }

    uint8_t alpha = 128; // 50% opacity

    // 1. Compute scalar reference
    blend_span_rgb565_scalar(dst_scalar, src, count, alpha);

    // 2. Compute NEON SIMD vectorized result
    blend_span_rgb565_neon(dst_neon, src, count, alpha);

    // 3. Verify 100% bit-exact correctness
    int mismatches = 0;
    for (int i = 0; i < count; i++) {
        if (dst_scalar[i] != dst_neon[i]) {
            mismatches++;
        }
    }

    if (mismatches > 0) {
        printf("FAIL: NEON blit had %d mismatches against scalar reference!\n", mismatches);
        return 1;
    }

    // 4. Test noclip bounding box rejection
    bool rejected = is_sprite_culled_noclip(-50, -50, 32, 32, 640, 360);
    assert(rejected == true);

    bool accepted = is_sprite_culled_noclip(100, 100, 32, 32, 640, 360);
    assert(accepted == false);

    printf("[+] NEON Blitter correctness and clipping test PASSED (0 mismatches).\n");
    return 0;
}
