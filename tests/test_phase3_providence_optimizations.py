import unittest
import os
import re

class TestPhase3ProvidenceOptimizations(unittest.TestCase):
    def setUp(self):
        self.root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.runner_h = os.path.join(self.root, "butterscotch", "src", "runner.h")
        self.runner_c = os.path.join(self.root, "butterscotch", "src", "runner.c")
        self.vm_builtins_c = os.path.join(self.root, "butterscotch", "src", "vm_builtins.c")
        self.sw_renderer_c = os.path.join(self.root, "butterscotch", "src", "sw_renderer.c")

    def test_runner_header_declares_phase3_indices(self):
        with open(self.runner_h, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        self.assertIn("int32_t efTrailObjIndex;", content)
        self.assertIn("int32_t guardBulletObjIndex;", content)

    def test_runner_initializes_phase3_indices(self):
        with open(self.runner_c, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        self.assertIn('strcmp(dataWin->objt.objects[oi].name, "oEfTrail") == 0', content)
        self.assertIn("runner->efTrailObjIndex = (int32_t)oi;", content)
        self.assertIn('strcmp(dataWin->objt.objects[oi].name, "oGuardBullet") == 0', content)
        self.assertIn("runner->guardBulletObjIndex = (int32_t)oi;", content)

    def test_ef_trail_pooling_prevents_draw_cache_invalidation(self):
        with open(self.vm_builtins_c, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        # In builtin_instance_destroy, oEfTrail is kept pooled (active=false, visible=false)
        destroy_section = re.search(r'builtin_instance_destroy\b.*?\n}', content, re.DOTALL)
        self.assertIsNotNone(destroy_section)
        self.assertIn("runner->efTrailObjIndex", destroy_section.group(0))
        self.assertIn("ctx->currentInstance->active = false;", destroy_section.group(0))
        self.assertIn("ctx->currentInstance->visible = false;", destroy_section.group(0))

        # In builtin_instance_create, oEfTrail is recycled from pool
        create_section = re.search(r'builtin_instance_create\b.*?\n}', content, re.DOTALL)
        self.assertIsNotNone(create_section)
        self.assertIn("runner->efTrailObjIndex", create_section.group(0))
        self.assertIn("trailCount >= 12", create_section.group(0))
        self.assertIn("recycled->imageAlpha = 1.0f;", create_section.group(0))

    def test_guard_bullet_concentric_circle_decimation(self):
        with open(self.vm_builtins_c, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        circle_section = re.search(r'builtin_draw_circle_color\b.*?\n}', content, re.DOTALL)
        self.assertIsNotNone(circle_section)
        self.assertIn("runner->guardBulletObjIndex", circle_section.group(0))
        self.assertIn("r > 13.0f && r < 19.0f", circle_section.group(0))

    def test_sw_renderer_circle_swar_acceleration(self):
        with open(self.sw_renderer_c, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        circle_func = re.search(r'swDrawCircleColor\b.*?\n}', content, re.DOTALL)
        self.assertIsNotNone(circle_func)
        body = circle_func.group(0)
        # Check SWAR constants hoisted outside scanline loops
        self.assertIn("const_s_rb", body)
        self.assertIn("const_s_g", body)
        self.assertIn("inv_a5", body)
        self.assertIn("isCircle", body)
        self.assertIn("count32", body)

if __name__ == "__main__":
    unittest.main()
