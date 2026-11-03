import os
import unittest

class TestLightningAndGuardBulletFixes(unittest.TestCase):
    def setUp(self):
        self.workspace_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.vm_builtins_path = os.path.join(self.workspace_dir, "butterscotch", "src", "vm_builtins.c")
        self.nclp_path = os.path.join(self.workspace_dir, "butterscotch", "src", "native_level_subsystem.c")
        self.prefs_path = os.path.join(self.workspace_dir, "dist", "Roms", "PORTS", "Games", "Risk of Rain", "Prefs.ini")

        with open(self.vm_builtins_path, "r", encoding="utf-8") as f:
            self.vm_builtins_content = f.read()
        with open(self.nclp_path, "r", encoding="utf-8") as f:
            self.nclp_content = f.read()
        with open(self.prefs_path, "r", encoding="utf-8") as f:
            self.prefs_content = f.read()

    def test_native_draw_lightning_implementation(self):
        """Verifies builtin_draw_lightning is implemented and registered in vm_builtins.c."""
        self.assertIn("builtin_draw_lightning", self.vm_builtins_content)
        self.assertIn('VM_registerBuiltin(ctx, "draw_lightning", builtin_draw_lightning);', self.vm_builtins_content)
        self.assertIn("drawLineColor", self.vm_builtins_content)
        self.assertIn("0xFFFFFF", self.vm_builtins_content) # Electric white core

    def test_draw_line_colour_color_parsing(self):
        """Verifies builtin_draw_line_colour uses RValue_toColour instead of RValue_toReal for color fidelity."""
        self.assertIn("uint32_t col1 = RValue_toColour(args[4]);", self.vm_builtins_content)
        self.assertIn("uint32_t col2 = RValue_toColour(args[5]);", self.vm_builtins_content)

    def test_guard_bullet_lifecycle_not_broken(self):
        """Verifies native_level_subsystem.c does NOT suppress oGuardBullet GML lifecycle."""
        # Ensure oGuardBullet STEP_END is not intercepted returning true
        self.assertNotIn("inst->objectIndex == s_obj_guard_bullet", self.nclp_content)

    def test_prefs_keeps_low_graphics_for_performance(self):
        """Verifies Prefs.ini keeps graphics=1.000000 to avoid heavy rain/water particle overhead on handheld."""
        self.assertIn("graphics=1.000000", self.prefs_content)

if __name__ == "__main__":
    unittest.main()
