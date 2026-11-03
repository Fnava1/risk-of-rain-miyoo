import os
import unittest

class TestDoorPauseAndCircleFixes(unittest.TestCase):
    def setUp(self):
        self.root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.vm_builtins = os.path.join(self.root_dir, "butterscotch", "src", "vm_builtins.c")
        self.native_subsystem = os.path.join(self.root_dir, "butterscotch", "src", "native_level_subsystem.c")
        self.assertTrue(os.path.exists(self.vm_builtins))
        self.assertTrue(os.path.exists(self.native_subsystem))

    def test_move_outside_does_not_warp_when_blocked(self):
        """moveOutsideCommon must keep origX, origY instead of jumping 1000px into the ceiling."""
        with open(self.vm_builtins, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static void moveOutsideCommon(")
        self.assertNotEqual(func_start, -1)
        func_end = src.find("static RValue builtin_move_outside_solid(", func_start)
        func_body = src[func_start:func_end]

        self.assertNotIn("inst->x = (float) (origX + (GMLReal) steps * dx);", func_body)
        self.assertNotIn("inst->y = (float) (origY + (GMLReal) steps * dy);", func_body)
        self.assertIn("inst->x = origX;", func_body)
        self.assertIn("inst->y = origY;", func_body)

    def test_instance_activate_skips_sector_hibernated(self):
        """instance_activate builtins must respect sectorHibernated flag to prevent unpause lag."""
        with open(self.vm_builtins, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        # Check builtin_instance_activate_all
        all_start = src.find("static RValue builtin_instance_activate_all(")
        self.assertNotEqual(all_start, -1)
        all_end = src.find("static RValue builtin_instance_activate_object(", all_start)
        all_body = src[all_start:all_end]
        self.assertIn("!instance->sectorHibernated", all_body)

        # Check builtin_instance_activate_object
        obj_start = all_end
        obj_end = src.find("static RValue builtin_instance_deactivate_object(", obj_start)
        obj_body = src[obj_start:obj_end]
        self.assertIn("!instance->sectorHibernated", obj_body)

        # Check builtin_instance_activate_region
        reg_start = src.find("static RValue builtin_instance_activate_region(")
        self.assertNotEqual(reg_start, -1)
        reg_end = src.find("static RValue builtin_instance_activate_layer(", reg_start)
        reg_body = src[reg_start:reg_end]
        self.assertIn("inst->sectorHibernated", reg_body)

    def test_nclp_periodic_hibernation_reinforcement(self):
        """NCLP_step must periodically ensure sector-hibernated instances remain inactive."""
        with open(self.native_subsystem, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("runner->sector_hibernation_active", src)
        self.assertIn("inst->sectorHibernated && inst->active", src)
        self.assertIn("inst->active = false;", src)

    def test_large_circle_outline_clamp(self):
        """builtin_draw_circle and builtin_draw_circle_color must clamp r > 64 filled circles to outline."""
        with open(self.vm_builtins, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        circle_start = src.find("static RValue builtin_draw_circle(")
        self.assertNotEqual(circle_start, -1)
        circle_end = src.find("static RValue builtin_draw_ellipse(", circle_start)
        circle_body = src[circle_start:circle_end]

        self.assertIn("if (!outline && r > 64.0f)", circle_body)
        self.assertIn("outline = true;", circle_body)

    def test_text_drawing_zero_alloc(self):
        """builtin_draw_text functions must use resolveTextArg to avoid safeStrdup when string is passed."""
        with open(self.vm_builtins, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("resolveTextArg", src)
        self.assertIn("const char* str = resolveTextArg(args[2], ctx->runner->dataWin, &ownedStr);", src)
        self.assertIn("const char* str = resolveTextArg(args[0], ctx->runner->dataWin, &ownedStr);", src)

if __name__ == "__main__":
    unittest.main()
