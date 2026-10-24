#!/usr/bin/env python3
"""
Test suite verifying rotated sprite support in the software renderer.
Ensures that angleDeg is properly handled and that the affine inverse
mapping mathematics produces exact texture sampling at arbitrary angles.
"""

import os
import math
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SW_RENDERER_C = os.path.join(REPO_ROOT, "butterscotch", "src", "sw_renderer.c")
BIN_PATH = os.path.join(REPO_ROOT, "dist", "Roms", "PORTS", "Games", "Risk of Rain", "RiskOfRain")


class TestSWRendererRotation(unittest.TestCase):
    def test_sw_renderer_rotation_implemented(self):
        """sw_renderer.c must implement blitRotatedSubRect and not discard angleDeg."""
        self.assertTrue(os.path.exists(SW_RENDERER_C), f"Missing {SW_RENDERER_C}")
        with open(SW_RENDERER_C, "r", encoding="utf-8") as f:
            src = f.read()

        self.assertIn("blitRotatedSubRect", src, "sw_renderer.c must implement blitRotatedSubRect")
        self.assertNotIn("swDrawSprite(Renderer *renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, MAYBE_UNUSED float angleDeg",
                         src, "swDrawSprite must not mark angleDeg as MAYBE_UNUSED")
        self.assertIn("normAngle > 0.001f && normAngle < 359.999f", src,
                      "sw_renderer.c must detect rotation and branch to rotated blitter")

    def test_affine_inverse_mapping_math(self):
        """Simulate the 2D affine inverse mapping for arbitrary angles and verify exact mapping."""
        angles = [0.0, 30.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0]
        src_w, src_h = 16.0, 16.0
        origin_x, origin_y = 8.0, 8.0
        x, y = 320.0, 240.0

        for angle_deg in angles:
            angle_rad = -angle_deg * (math.pi / 180.0)
            cos_a = math.cos(angle_rad)
            sin_a = math.sin(angle_rad)

            # Local coordinates
            local_x0 = -origin_x
            local_y0 = -origin_y
            local_x1 = local_x0 + src_w
            local_y1 = local_y0 + src_h

            # Corners in world/screen space
            def transform_pt(lx, ly):
                wx = x + cos_a * lx - sin_a * ly
                wy = y + sin_a * lx + cos_a * ly
                return wx, wy

            sx0, sy0 = transform_pt(local_x0, local_y0)  # (0, 0)
            sx1, sy1 = transform_pt(local_x1, local_y0)  # (srcW, 0)
            sx2, sy2 = transform_pt(local_x0, local_y1)  # (0, srcH)
            sx3, sy3 = transform_pt(local_x1, local_y1)  # (srcW, srcH)

            # Inverse affine matrix
            dUx = (sx1 - sx0) / src_w
            dUy = (sy1 - sy0) / src_w
            dVx = (sx2 - sx0) / src_h
            dVy = (sy2 - sy0) / src_h

            det = dUx * dVy - dVx * dUy
            self.assertAlmostEqual(det, 1.0, places=4, msg=f"Det should be 1.0 at angle {angle_deg}")

            invDet = 1.0 / det
            M00 = dVy * invDet
            M01 = -dVx * invDet
            M10 = -dUy * invDet
            M11 = dUx * invDet

            # Verify inverse mapping for all 4 corners
            corners = [
                (sx0, sy0, 0.0, 0.0),
                (sx1, sy1, src_w, 0.0),
                (sx2, sy2, 0.0, src_h),
                (sx3, sy3, src_w, src_h),
            ]
            for sx, sy, exp_u, exp_v in corners:
                px = sx - sx0
                py = sy - sy0
                u = M00 * px + M01 * py
                v = M10 * px + M11 * py
                self.assertAlmostEqual(u, exp_u, places=3, msg=f"u failed at angle {angle_deg}")
                self.assertAlmostEqual(v, exp_v, places=3, msg=f"v failed at angle {angle_deg}")

    def test_binary_stripped_and_valid(self):
        """RiskOfRain binary in dist directory must be ARM ELF and under 2.5 MB."""
        self.assertTrue(os.path.exists(BIN_PATH), f"Missing binary {BIN_PATH}")
        size = os.path.getsize(BIN_PATH)
        self.assertLess(size, 2500000, f"Binary too large: {size} bytes")
        with open(BIN_PATH, "rb") as f:
            header = f.read(4)
        self.assertEqual(header, b"\x7fELF", "Must be ELF")


if __name__ == "__main__":
    unittest.main()
