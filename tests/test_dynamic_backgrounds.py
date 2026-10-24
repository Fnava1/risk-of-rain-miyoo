import unittest
import os
import re

class TestDynamicBackgrounds(unittest.TestCase):
    """
    Tests for dynamic background creation and lifecycle management
    supporting background_create_color, background_delete, and related functions.
    Prevents the bug where missing background_create_color causes HUD invincibility
    to stretch background 0 (tileset) across the bottom-right of the screen.
    """

    def setUp(self):
        self.repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.butterscotch_src = os.path.join(self.repo_root, "butterscotch", "src")

    def test_sw_renderer_dynamic_background_implementation(self):
        sw_renderer_path = os.path.join(self.butterscotch_src, "sw_renderer.c")
        self.assertTrue(os.path.exists(sw_renderer_path), "sw_renderer.c must exist")
        with open(sw_renderer_path, "r", encoding="utf-8", errors="ignore") as f:
            code = f.read()

        self.assertIn("swCreateBackgroundFromColor", code, "sw_renderer.c must implement swCreateBackgroundFromColor")
        self.assertIn("swDeleteBackground", code, "sw_renderer.c must implement swDeleteBackground")
        self.assertIn("swVtable.createBackgroundFromColor = swCreateBackgroundFromColor", code,
                      "swVtable must register createBackgroundFromColor")
        self.assertIn("swVtable.deleteBackground = swDeleteBackground", code,
                      "swVtable must register deleteBackground")

        # ensureTextureLoaded must prioritize loaded dynamic pages before checking data.win txtr.count
        ensure_idx = code.find("ensureTextureLoaded")
        self.assertNotEqual(ensure_idx, -1)
        func_body = code[ensure_idx:ensure_idx + 400]
        self.assertIn("sw->pages[pageId].loaded", func_body)

    def test_vm_builtins_background_registration(self):
        vm_builtins_path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        self.assertTrue(os.path.exists(vm_builtins_path), "vm_builtins.c must exist")
        with open(vm_builtins_path, "r", encoding="utf-8", errors="ignore") as f:
            code = f.read()

        self.assertIn("builtin_background_create_color", code)
        self.assertIn("builtin_background_delete", code)
        self.assertIn("builtin_background_exists", code)
        self.assertIn("builtin_background_get_name", code)

        self.assertIn('VM_registerBuiltin(ctx, "background_create_color", builtin_background_create_color)', code)
        self.assertIn('VM_registerBuiltin(ctx, "background_create_colour", builtin_background_create_color)', code)
        self.assertIn('VM_registerBuiltin(ctx, "background_delete", builtin_background_delete)', code)
        self.assertIn('VM_registerBuiltin(ctx, "background_exists", builtin_background_exists)', code)

    def test_data_win_allocators(self):
        data_win_path = os.path.join(self.butterscotch_src, "data_win.c")
        self.assertTrue(os.path.exists(data_win_path), "data_win.c must exist")
        with open(data_win_path, "r", encoding="utf-8", errors="ignore") as f:
            code = f.read()

        self.assertIn("DataWin_allocBackgroundSlot", code)
        self.assertIn("DataWin_allocTpagSlot", code)

    def test_scaled_blit_mapping_simulation(self):
        """
        Simulate 1x7 background scaled by 160x1 (HUD invincibility box).
        Verify destination bounds are strictly 160x7 pixels without screen overshoot.
        """
        src_w, src_h = 1, 7
        xscale, yscale = 160.0, 1.0
        dst_x, dst_y = 240, 445  # Example HUD position on 640x480 screen

        draw_w = int(src_w * abs(xscale))
        draw_h = int(src_h * abs(yscale))

        self.assertEqual(draw_w, 160, "Scaled width must be exactly 160 pixels")
        self.assertEqual(draw_h, 7, "Scaled height must be exactly 7 pixels")

        # Destination pixel bounding box
        x0, y0 = dst_x, dst_y
        x1, y1 = dst_x + draw_w, dst_y + draw_h

        self.assertEqual(x1 - x0, 160)
        self.assertEqual(y1 - y0, 7)

        # Coordinate sampling math: each column in [0, 160) maps to source column 0
        inv_scale_x = 1.0 / abs(xscale)
        step_x = int(inv_scale_x * 65536.0 + 0.5)

        cur_x = 0
        for x in range(draw_w):
            sample_sx = cur_x >> 16
            self.assertEqual(sample_sx, 0, "Source X must always sample pixel 0 of 1x7 texture")
            cur_x += step_x

        # Source Y in [0, 7) maps 1:1 with yscale=1
        inv_scale_y = 1.0 / abs(yscale)
        step_y = int(inv_scale_y * 65536.0 + 0.5)
        cur_y = 0
        for y in range(draw_h):
            sample_sy = cur_y >> 16
            self.assertEqual(sample_sy, y, f"Source Y must map directly to row {y}")
            cur_y += step_y

if __name__ == "__main__":
    unittest.main()
