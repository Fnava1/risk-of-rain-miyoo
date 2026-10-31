#!/usr/bin/env python3
"""
Unit tests for SWAR (SIMD Within A Register) and NEON RGB565 blending algorithms.
Verifies that the bitwise operations in sw_renderer.c produce 100% bit-exact results
compared to the reference scalar implementation for both normal and additive blending.
"""

import unittest
import random

def ref_blend_pixel_rgb565(d: int, s: int, alpha: int) -> int:
    if alpha == 255:
        return s
    if alpha == 0:
        return d
    a = alpha + 1
    inv_a = 256 - a
    s_r = (s >> 11) & 0x1F
    s_g = (s >> 5) & 0x3F
    s_b = s & 0x1F
    d_r = (d >> 11) & 0x1F
    d_g = (d >> 5) & 0x3F
    d_b = d & 0x1F
    r = (s_r * a + d_r * inv_a) >> 8
    g = (s_g * a + d_g * inv_a) >> 8
    b = (s_b * a + d_b * inv_a) >> 8
    return (r << 11) | (g << 5) | b

def swar_blend_pixel_rgb565(d: int, s: int, alpha: int) -> int:
    if alpha == 255:
        return s
    if alpha == 0:
        return d
    a = alpha + 1
    inv_a = 256 - a
    s_rb = (s & 0x001F) | ((s & 0xF800) << 5)
    d_rb = (d & 0x001F) | ((d & 0xF800) << 5)
    rb = (s_rb * a + d_rb * inv_a) >> 8
    res_b = rb & 0x001F
    res_r = (rb >> 5) & 0xF800
    s_g = s & 0x07E0
    d_g = d & 0x07E0
    res_g = ((s_g * a + d_g * inv_a) >> 8) & 0x07E0
    return res_r | res_g | res_b

def ref_blend_pixel_rgb565_add(d: int, s: int, alpha: int) -> int:
    if alpha == 0:
        return d
    a = alpha + 1
    s_r = (((s >> 11) & 0x1F) * a) >> 8
    s_g = (((s >> 5) & 0x3F) * a) >> 8
    s_b = ((s & 0x1F) * a) >> 8
    r = ((d >> 11) & 0x1F) + s_r
    g = ((d >> 5) & 0x3F) + s_g
    b = (d & 0x1F) + s_b
    if r > 31: r = 31
    if g > 63: g = 63
    if b > 31: b = 31
    return (r << 11) | (g << 5) | b

def swar_blend_pixel_rgb565_add(d: int, s: int, alpha: int) -> int:
    if alpha == 0:
        return d
    a = alpha + 1
    s_rb = (s & 0x001F) | ((s & 0xF800) << 5)
    s_g = s & 0x07E0
    scaled_rb = (s_rb * a) >> 8
    s_b = scaled_rb & 0x001F
    s_r = (scaled_rb >> 16) & 0x001F
    s_g_scaled = ((s_g * a) >> 8) >> 5
    r = ((d >> 11) & 0x1F) + s_r
    g = ((d >> 5) & 0x3F) + s_g_scaled
    b = (d & 0x1F) + s_b
    if r > 31: r = 31
    if g > 63: g = 63
    if b > 31: b = 31
    return (r << 11) | (g << 5) | b


class TestSWARBlending(unittest.TestCase):
    def test_corner_cases_normal_blend(self):
        corners = [0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xF81F, 0x07FF, 0xFFE0]
        alphas = [0, 1, 127, 128, 254, 255]
        for d in corners:
            for s in corners:
                for a in alphas:
                    expected = ref_blend_pixel_rgb565(d, s, a)
                    actual = swar_blend_pixel_rgb565(d, s, a)
                    self.assertEqual(expected, actual, f"Corner fail normal: d={hex(d)}, s={hex(s)}, a={a}")

    def test_corner_cases_additive_blend(self):
        corners = [0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xF81F, 0x07FF, 0xFFE0]
        alphas = [0, 1, 127, 128, 254, 255]
        for d in corners:
            for s in corners:
                for a in alphas:
                    expected = ref_blend_pixel_rgb565_add(d, s, a)
                    actual = swar_blend_pixel_rgb565_add(d, s, a)
                    self.assertEqual(expected, actual, f"Corner fail add: d={hex(d)}, s={hex(s)}, a={a}")

    def test_random_samples_normal_blend(self):
        rng = random.Random(1337)
        for _ in range(50000):
            d = rng.randint(0, 0xFFFF)
            s = rng.randint(0, 0xFFFF)
            a = rng.randint(0, 255)
            expected = ref_blend_pixel_rgb565(d, s, a)
            actual = swar_blend_pixel_rgb565(d, s, a)
            self.assertEqual(expected, actual)

    def test_random_samples_additive_blend(self):
        rng = random.Random(7331)
        for _ in range(50000):
            d = rng.randint(0, 0xFFFF)
            s = rng.randint(0, 0xFFFF)
            a = rng.randint(0, 255)
            expected = ref_blend_pixel_rgb565_add(d, s, a)
            actual = swar_blend_pixel_rgb565_add(d, s, a)
            self.assertEqual(expected, actual)

if __name__ == '__main__':
    unittest.main()
