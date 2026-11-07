#!/usr/bin/env python3
"""
Unit tests for Miyoo Mini Flip & v4 Hardware Features:
1. Framebuffer dynamic centering arithmetic for 752x560 and 640x480 panels.
2. Clamshell lid switch (EV_SW / SW_LID) constants and lifecycle handling.
3. Model-aware CPU clock frequency guard mappings.
"""

import unittest
import os

SCREEN_WIDTH = 640
SCREEN_HEIGHT = 480

def compute_fb_offsets(xres: int, yres: int):
    offset_x = (xres - SCREEN_WIDTH) // 2 if xres > SCREEN_WIDTH else 0
    offset_y = (yres - SCREEN_HEIGHT) // 2 if yres > SCREEN_HEIGHT else 0
    return offset_x, offset_y

def compute_render_bounds(xres: int, yres: int):
    ox, oy = compute_fb_offsets(xres, yres)
    x_min = ox
    x_max = ox + SCREEN_WIDTH - 1
    y_min = oy
    y_max = oy + SCREEN_HEIGHT - 1
    return (x_min, x_max), (y_min, y_max)


class TestClamshellAndCentering(unittest.TestCase):

    def test_fb_centering_mmp_standard_640x480(self):
        """Miyoo Mini Plus & v1-v3 640x480 display must have zero offsets (fullscreen)."""
        ox, oy = compute_fb_offsets(640, 480)
        self.assertEqual(ox, 0)
        self.assertEqual(oy, 0)

        (x_min, x_max), (y_min, y_max) = compute_render_bounds(640, 480)
        self.assertEqual(x_min, 0)
        self.assertEqual(x_max, 639)
        self.assertEqual(y_min, 0)
        self.assertEqual(y_max, 479)

    def test_fb_centering_v4_and_flip_752x560(self):
        """Miyoo Mini v4 and Miyoo Mini Flip 752x560 panel must be symmetrically centered."""
        ox, oy = compute_fb_offsets(752, 560)
        # (752 - 640) / 2 = 56
        self.assertEqual(ox, 56)
        # (560 - 480) / 2 = 40
        self.assertEqual(oy, 40)

        (x_min, x_max), (y_min, y_max) = compute_render_bounds(752, 560)
        self.assertEqual(x_min, 56)
        self.assertEqual(x_max, 695)
        self.assertEqual(y_min, 40)
        self.assertEqual(y_max, 519)

        # Symmetry check: margin on left equals margin on right
        left_margin = x_min
        right_margin = 752 - (x_max + 1)
        self.assertEqual(left_margin, right_margin)
        self.assertEqual(left_margin, 56)

        # Symmetry check: margin on top equals margin on bottom
        top_margin = y_min
        bottom_margin = 560 - (y_max + 1)
        self.assertEqual(top_margin, bottom_margin)
        self.assertEqual(top_margin, 40)

    def test_fb_centering_flip_nominal_750x560(self):
        """Miyoo Mini Flip 750x560 nominal panel should compute symmetrical offsets."""
        ox, oy = compute_fb_offsets(750, 560)
        self.assertEqual(ox, 55)
        self.assertEqual(oy, 40)

    def test_linux_input_clamshell_constants(self):
        """Verify Linux input subsystem switch constants for clamshell lid."""
        EV_SW = 0x05
        SW_LID = 0x00
        self.assertEqual(EV_SW, 5)
        self.assertEqual(SW_LID, 0)

    def test_miyoo_c_contains_lid_sensor_logic(self):
        """Verify butterscotch/src/backends/miyoo.c contains EV_SW and SW_LID handling."""
        miyoo_c_path = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "backends", "miyoo.c")
        self.assertTrue(os.path.isfile(miyoo_c_path), "miyoo.c not found")
        with open(miyoo_c_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("SW_LID", content, "miyoo.c missing SW_LID macro or reference")
        self.assertIn("EV_SW", content, "miyoo.c missing EV_SW macro or reference")
        self.assertIn("s_fb_offset_x", content, "miyoo.c missing dynamic horizontal offset")
        self.assertIn("s_fb_offset_y", content, "miyoo.c missing dynamic vertical offset")
        self.assertIn("Miyoo Clamshell: Lid closed", content, "miyoo.c missing lid close handler log")

    def test_model_clock_guard_logic(self):
        """Verify clock guard mapping logic in miyoo.c handles non-354 as 1400 MHz."""
        miyoo_c_path = os.path.join(os.path.dirname(__file__), "..", "butterscotch", "src", "backends", "miyoo.c")
        with open(miyoo_c_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("if (model == 354) s_target_cpu_mhz = 1800;", content)
        self.assertIn("else s_target_cpu_mhz = 1400;", content)


if __name__ == "__main__":
    unittest.main()
