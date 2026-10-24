import unittest
import os
import math

class TestCircleAndCollisionOptimizations(unittest.TestCase):
    def setUp(self):
        self.root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.butterscotch_src = os.path.join(self.root_dir, "butterscotch", "src")

    def test_renderer_h_vtable_and_fallback(self):
        """RendererVtable must declare drawCircleColor and Renderer_drawCircleColor must call it."""
        path = os.path.join(self.butterscotch_src, "renderer.h")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("void (*drawCircleColor)(Renderer* renderer", src,
                      "RendererVtable must declare drawCircleColor")
        self.assertIn("if (renderer->vtable->drawCircleColor != NULL)", src,
                      "Renderer_drawCircleColor must check for direct vtable hook")
        self.assertIn("renderer->vtable->drawCircleColor(renderer, cx, cy, radius, col1, col2, renderer->drawAlpha, outline);", src,
                      "Renderer_drawCircleColor must delegate to vtable->drawCircleColor when present")

    def test_sw_renderer_implements_circle_rasterizer(self):
        """sw_renderer.c must implement swDrawCircleColor and register it in swVtable."""
        path = os.path.join(self.butterscotch_src, "sw_renderer.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("static void swDrawCircleColor(Renderer *renderer", src,
                      "sw_renderer.c must implement swDrawCircleColor")
        self.assertIn("swVtable.drawCircleColor = swDrawCircleColor;", src,
                      "swVtable must register drawCircleColor")
        # Verify midpoint algorithm and scanline presence
        self.assertIn("PLOT_CIRCLE_PIXEL(icx + x, icy + y)", src,
                      "swDrawCircleColor must use midpoint circle algorithm for outline")
        self.assertIn("srx * sqrtf(1.0f - ratio)", src,
                      "swDrawCircleColor must use scanline span filler for solid fill")

    def test_builtin_collision_rectangle_uses_spatial_grid(self):
        """builtin_collision_rectangle must use SpatialGrid instead of linear snapshot scan."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        # Find builtin_collision_rectangle
        func_start = src.find("builtin_collision_rectangle(VMContext* ctx")
        self.assertNotEqual(func_start, -1, "Missing builtin_collision_rectangle")
        func_end = src.find("builtin_collision_circle(", func_start)
        func_body = src[func_start:func_end]

        self.assertIn("SpatialGrid_syncGrid", func_body,
                      "builtin_collision_rectangle must sync spatial grid")
        self.assertIn("SpatialGrid_prepareQuery", func_body,
                      "builtin_collision_rectangle must use SpatialGrid_prepareQuery")
        self.assertNotIn("Runner_pushInstancesForTarget", func_body,
                         "builtin_collision_rectangle must not use linear Runner_pushInstancesForTarget")

    def test_circle_scanline_math_simulation(self):
        """Simulate circle scanline span filler for Wandering Vagrant missile radius (r=6)."""
        radius = 6.0
        scx, scy = 100.0, 100.0
        y0 = int(math.ceil(scy - radius))
        y1 = int(math.floor(scy + radius))

        pixels_filled = 0
        for y in range(y0, y1 + 1):
            dy = float(y) - scy
            ratio = (dy * dy) / (radius * radius)
            if ratio > 1.0:
                continue
            dx = radius * math.sqrt(1.0 - ratio)
            x0 = int(round(scx - dx))
            x1 = int(round(scx + dx))
            pixels_filled += (x1 - x0 + 1)

        # Theoretical circle area = pi * 6^2 = 113.1
        self.assertGreaterEqual(pixels_filled, 100)
        self.assertLessEqual(pixels_filled, 125)


if __name__ == "__main__":
    unittest.main()
