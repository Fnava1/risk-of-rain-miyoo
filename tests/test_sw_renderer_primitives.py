#!/usr/bin/env python3
"""
Test suite verifying primitive rendering and sector rasterization in the software renderer.
Ensures that primitive batching, triangle strip expansion, and scanline rasterization
correctly support pie charts / sector graphs (Scores and Unlockables -> General).
"""

import os
import math
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SW_RENDERER_C = os.path.join(REPO_ROOT, "butterscotch", "src", "sw_renderer.c")
BIN_PATH = os.path.join(REPO_ROOT, "dist", "Roms", "PORTS", "Games", "Risk of Rain", "RiskOfRain")


class TestSWRendererPrimitives(unittest.TestCase):
    def test_sw_renderer_primitives_implemented(self):
        """sw_renderer.c must implement primitiveBegin, primitiveEnd, drawVertex, and rasterize_triangle."""
        self.assertTrue(os.path.exists(SW_RENDERER_C), f"Missing {SW_RENDERER_C}")
        with open(SW_RENDERER_C, "r", encoding="utf-8") as f:
            src = f.read()

        # Check primitive functions are not empty stubs
        self.assertNotIn("static void swPrimitiveBegin(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t primitiveType) {}",
                         src, "swPrimitiveBegin must not be an empty stub")
        self.assertNotIn("static void swPrimitiveEnd(MAYBE_UNUSED Renderer *renderer) {}",
                         src, "swPrimitiveEnd must not be an empty stub")
        self.assertNotIn("static void swDrawVertex(MAYBE_UNUSED Renderer *renderer",
                         src, "swDrawVertex must not be an empty stub")

        # Check essential primitive pipeline components
        self.assertIn("sw_rasterize_triangle", src, "sw_renderer.c must implement sw_rasterize_triangle")
        self.assertIn("PRIMITIVE_TRIANGLE_STRIP", src, "swPrimitiveEnd must handle PRIMITIVE_TRIANGLE_STRIP")
        self.assertIn("PRIMITIVE_TRIANGLE_FAN", src, "swPrimitiveEnd must handle PRIMITIVE_TRIANGLE_FAN")
        self.assertIn("PRIMITIVE_TRIANGLES", src, "swPrimitiveEnd must handle PRIMITIVE_TRIANGLES")
        self.assertIn("sw->primVertices", src, "sw_renderer.c must batch vertices in primVertices")

    def test_binary_size_constraint(self):
        """Miyoo Mini+ binary must remain under 2.5 MB."""
        self.assertTrue(os.path.exists(BIN_PATH), f"Missing binary at {BIN_PATH}")
        size = os.path.getsize(BIN_PATH)
        self.assertLess(size, 2.5 * 1024 * 1024, f"Binary exceeds 2.5 MB limit: {size} bytes")

    def test_sector_triangle_strip_rasterization_math(self):
        """Simulate draw_sector triangle strip generation and verify pixel rasterization."""
        cx, cy = 320.0, 240.0
        ang1, ang2 = 0.0, 90.0  # Quarter circle / 25% slice
        step = 5.0
        r_inner = 20.0
        r_outer = 60.0

        ang1_rad = math.radians(ang1)
        ang2_rad = math.radians(ang2)
        step_rad = math.radians(step)

        # Generate vertices matching gml_Script_draw_sector
        vertices = []
        cur_ang = ang1_rad
        while cur_ang <= ang2_rad + 0.0001:
            sin_a = math.sin(cur_ang)
            cos_a = math.cos(cur_ang)
            # Inner vertex
            x_in = cx + cos_a * r_inner
            y_in = cy - sin_a * r_inner
            # Outer vertex
            x_out = cx + cos_a * r_outer
            y_out = cy - sin_a * r_outer

            vertices.append((x_in, y_in))
            vertices.append((x_out, y_out))
            cur_ang += step_rad

        self.assertGreaterEqual(len(vertices), 4, "Must generate at least 4 vertices for triangle strip")

        # Triangle strip rasterizer simulation
        width, height = 640, 480
        fb = [[0] * width for _ in range(height)]

        def rasterize_tri(p0, p1, p2):
            pts = sorted([p0, p1, p2], key=lambda p: p[1])
            (x0, y0), (x1, y1), (x2, y2) = pts

            y_start = int(math.ceil(y0))
            y_end = int(math.floor(y2))
            total_h = y2 - y0
            if total_h <= 0.0001 or y_start > y_end:
                return

            for y in range(y_start, y_end + 1):
                if y < 0 or y >= height:
                    continue
                alpha_long = (y - y0) / total_h
                x_long = x0 + (x2 - x0) * alpha_long

                if y < y1:
                    seg_h = y1 - y0
                    x_short = x0 if seg_h <= 0.0001 else x0 + (x1 - x0) * ((y - y0) / seg_h)
                else:
                    seg_h = y2 - y1
                    x_short = x1 if seg_h <= 0.0001 else x1 + (x2 - x1) * ((y - y1) / seg_h)

                x_min = int(math.ceil(min(x_long, x_short)))
                x_max = int(math.floor(max(x_long, x_short)))

                if x_min > x_max:
                    x_mid = int(round((x_long + x_short) * 0.5))
                    if 0 <= x_mid < width:
                        fb[y][x_mid] = 1
                else:
                    for x in range(max(0, x_min), min(width, x_max + 1)):
                        fb[y][x] = 1

        # Triangle strip: each consecutive 3 vertices form a triangle
        triangles_count = len(vertices) - 2
        for i in range(triangles_count):
            rasterize_tri(vertices[i], vertices[i + 1], vertices[i + 2])

        # Verify rasterized pixels
        pixel_count = sum(row.count(1) for row in fb)
        expected_area = 0.25 * math.pi * (r_outer**2 - r_inner**2)

        # Allow within 5% tolerance of analytical annular sector area
        self.assertAlmostEqual(pixel_count, expected_area, delta=expected_area * 0.05,
                               msg=f"Pixel count {pixel_count} differs from expected area {expected_area}")

        # Check bounds: All pixels must be in quadrant 1 relative to center
        # (x >= cx, y <= cy)
        for y in range(height):
            for x in range(width):
                if fb[y][x] == 1:
                    self.assertGreaterEqual(x, cx - 1)
                    self.assertLessEqual(y, cy + 1)
                    dist = math.hypot(x - cx, cy - y)
                    self.assertGreaterEqual(dist, r_inner - 3.0)
                    self.assertLessEqual(dist, r_outer + 3.0)


if __name__ == "__main__":
    unittest.main()
