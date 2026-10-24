import unittest
import os

class TestWaterAndSmallElements(unittest.TestCase):
    def setUp(self):
        self.root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.butterscotch_src = os.path.join(self.root_dir, "butterscotch", "src")

    def test_sw_draw_rectangle_constant_alpha_fastpath(self):
        """swDrawRectangle must implement fast-path for solid fill with alpha blending."""
        path = os.path.join(self.butterscotch_src, "sw_renderer.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static void swDrawRectangle(Renderer *renderer")
        self.assertNotEqual(func_start, -1, "Missing swDrawRectangle")
        func_end = src.find("static void swDrawRectangleColor(", func_start)
        func_body = src[func_start:func_end]

        # Must precompute constant terms outside the pixel loop
        self.assertTrue("const_s_rb" in func_body or "s_r_a" in func_body or "inv_a" in func_body,
                        "swDrawRectangle must precalculate constant alpha blend terms outside pixel loop")

    def test_sw_draw_circle_small_radius_fastpath(self):
        """swDrawCircleColor must have a small-radius fast-path avoiding sqrtf for r <= 6."""
        path = os.path.join(self.butterscotch_src, "sw_renderer.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static void swDrawCircleColor(Renderer *renderer")
        self.assertNotEqual(func_start, -1, "Missing swDrawCircleColor")
        func_end = src.find("static void swDrawText(", func_start)
        func_body = src[func_start:func_end]

        self.assertTrue("small_circle" in func_body or "ir <= 6" in func_body or "r <= 6" in func_body,
                        "swDrawCircleColor must have a fast-path for small radii (r <= 6)")

    def test_builtin_instance_nearest_var_yoyo_registered(self):
        """instance_nearest_var_yoyo must be registered as a native C builtin in vm_builtins.c."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("builtin_instance_nearest_var_yoyo", src,
                      "vm_builtins.c must implement builtin_instance_nearest_var_yoyo")
        self.assertIn('VM_registerBuiltin(ctx, "instance_nearest_var_yoyo", builtin_instance_nearest_var_yoyo);', src,
                      "vm_builtins.c must register instance_nearest_var_yoyo builtin")

    def test_damage_instance_capping(self):
        """Runner and vm_builtins must track and cap floating damage instances (oEfDamage)."""
        runner_h = os.path.join(self.butterscotch_src, "runner.h")
        self.assertTrue(os.path.exists(runner_h), f"Missing {runner_h}")
        with open(runner_h, "r", encoding="utf-8", errors="ignore") as f:
            src_h = f.read()

        self.assertIn("damageObjIndex", src_h,
                      "Runner struct in runner.h must contain damageObjIndex")

        builtins_c = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(builtins_c, "r", encoding="utf-8", errors="ignore") as f:
            src_b = f.read()

        self.assertIn("damageObjIndex", src_b,
                      "builtin_instance_create must check runner->damageObjIndex for capping")

if __name__ == "__main__":
    unittest.main()
