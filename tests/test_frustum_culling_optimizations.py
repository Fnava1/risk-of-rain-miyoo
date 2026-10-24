import unittest
import os

class TestFrustumCullingOptimizations(unittest.TestCase):
    def setUp(self):
        self.root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.butterscotch_src = os.path.join(self.root_dir, "butterscotch", "src")

    def test_particles_draw_system_culling(self):
        """Particles_drawSystem in vm_builtins.c must cull off-screen particles before sprite draw and blend checks."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("void Particles_drawSystem(Runner* runner, int32_t systemId)")
        self.assertNotEqual(func_start, -1, "Missing Particles_drawSystem definition")
        func_end = src.find("static RValue builtin_part_system_drawit(", func_start)
        func_body = src[func_start:func_end]

        self.assertIn("cullActive", func_body, "Particles_drawSystem must have cullActive flag")
        self.assertIn("runner->activeViewValid && !runner->inGuiPass", func_body,
                      "Particles_drawSystem must check activeViewValid and !inGuiPass")
        self.assertIn("activeViewX", func_body, "Particles_drawSystem must check camera activeViewX")
        self.assertIn("activeViewY", func_body, "Particles_drawSystem must check camera activeViewY")

    def test_runner_draw_instance_normally_culling(self):
        """drawInstanceNormally in runner.c must cull off-screen instances only when codeId < 0."""
        path = os.path.join(self.butterscotch_src, "runner.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static void drawInstanceNormally(Runner* runner, Instance* inst)")
        self.assertNotEqual(func_start, -1, "Missing drawInstanceNormally definition")
        func_end = src.find("void Runner_draw(", func_start)
        func_body = src[func_start:func_end]

        self.assertIn("Runner_executeResolvedEvent(runner, inst, EVENT_DRAW, DRAW_NORMAL, codeId, ownerObjectIndex);", func_body,
                      "drawInstanceNormally must execute resolved draw event without culling when codeId >= 0")
        self.assertIn("runner->activeViewValid && !runner->inGuiPass", func_body,
                      "drawInstanceNormally must check activeViewValid and !inGuiPass for default draw_self")
        self.assertIn("inst->x < runner->activeViewX", func_body,
                      "drawInstanceNormally must cull off-screen instances")

    def test_builtin_draw_self_culling(self):
        """builtin_draw_self in vm_builtins.c must cull off-screen instances with camera views active."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static RValue builtin_draw_self(VMContext* ctx")
        self.assertNotEqual(func_start, -1, "Missing builtin_draw_self definition")
        func_end = src.find("static RValue builtin_draw_point(", func_start)
        func_body = src[func_start:func_end]

        self.assertIn("runner->activeViewValid && !runner->inGuiPass", func_body,
                      "builtin_draw_self must check activeViewValid and !inGuiPass")
        self.assertIn("activeViewX", func_body, "builtin_draw_self must check camera bounds")

    def test_frustum_culling_simulation(self):
        """Simulate viewport culling with safety margin on 640x480 view."""
        cam_x, cam_y = 1000.0, 500.0
        cam_w, cam_h = 640.0, 480.0
        margin = 128.0

        min_x = cam_x - margin
        max_x = cam_x + cam_w + margin
        min_y = cam_y - margin
        max_y = cam_y + cam_h + margin

        def should_cull(x, y):
            return x < min_x or x > max_x or y < min_y or y > max_y

        # Center of screen -> must NOT cull
        self.assertFalse(should_cull(1320.0, 740.0))

        # Right at the edge of screen (1000, 500) -> must NOT cull
        self.assertFalse(should_cull(1000.0, 500.0))

        # 50px outside screen border -> inside margin -> must NOT cull (safety buffer)
        self.assertFalse(should_cull(950.0, 500.0))
        self.assertFalse(should_cull(1680.0, 500.0))

        # Far outside (distant room coordinates) -> MUST cull
        self.assertTrue(should_cull(100.0, 100.0))
        self.assertTrue(should_cull(3500.0, 1800.0))

if __name__ == "__main__":
    unittest.main()
