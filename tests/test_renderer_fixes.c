#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "src/renderer/SoftwareRenderer.h"

int main() {
    printf("[*] Running Renderer Color Degradation & Opacity Fix Test...\n");

    uint16_t src[8] = { 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800 }; // Full red (R=31, G=0, B=0)
    uint16_t dst[8] = { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };

    // Blend at full opacity (alpha = 255)
    blend_span_rgb565_scalar(dst, src, 8, 255);

    // Full red must NOT be dimmed from 31 to 30!
    for (int i = 0; i < 8; i++) {
        uint32_t r = (dst[i] >> 11) & 0x1F;
        if (r != 31) {
            printf("FAIL: Full opacity (alpha=255) dimmed red channel: got R=%u, expected R=31\n", r);
            return 1;
        }
    }

    // Now test NEON at full opacity
    uint16_t dst_neon[8] = { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };
    blend_span_rgb565_neon(dst_neon, src, 8, 255);
    for (int i = 0; i < 8; i++) {
        uint32_t r = (dst_neon[i] >> 11) & 0x1F;
        if (r != 31) {
            printf("FAIL: NEON full opacity (alpha=255) dimmed red channel: got R=%u, expected R=31\n", r);
            return 1;
        }
    }

    printf("[+] Renderer opacity test PASSED (no color degradation at alpha=255).\n");
    return 0;
}
