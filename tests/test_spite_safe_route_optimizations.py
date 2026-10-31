import os
import unittest

class TestSpiteSafeRouteOptimizations(unittest.TestCase):
    def setUp(self):
        self.root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.runner_h = os.path.join(self.root, "butterscotch", "src", "runner.h")
        self.runner_c = os.path.join(self.root, "butterscotch", "src", "runner.c")
        self.audio_c = os.path.join(self.root, "butterscotch", "src", "audio", "miniaudio", "ma_audio_system.c")

    def test_explosion_index_in_runner_h(self):
        """Runner struct in runner.h must track explosionObjIndex."""
        self.assertTrue(os.path.exists(self.runner_h))
        with open(self.runner_h, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        self.assertIn("int32_t explosionObjIndex;", content)

    def test_explosion_index_initialized_in_runner_c(self):
        """Runner_create must initialize explosionObjIndex matching oExplosion."""
        self.assertTrue(os.path.exists(self.runner_c))
        with open(self.runner_c, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        self.assertIn("runner->explosionObjIndex = -1;", content)
        self.assertIn('strcmp(dataWin->objt.objects[oi].name, "oExplosion") == 0', content)

    def test_explosion_viewport_culling_in_draw(self):
        """drawInstanceNormally must frustum cull off-screen oExplosion instances."""
        with open(self.runner_c, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        self.assertIn("inst->objectIndex == runner->explosionObjIndex", content)
        self.assertIn("runner->activeViewX - margin", content)
        self.assertIn("runner->activeViewY - margin", content)

    def test_audio_voice_limiting_in_ma_audio_system(self):
        """maPlaySound must limit duplicate non-looping sound instances and steal oldest voice safely."""
        self.assertTrue(os.path.exists(self.audio_c))
        with open(self.audio_c, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        self.assertIn("activeForThisSound >= 4", content)
        self.assertIn("maSeekInstance(oldestMatching, 0.0f);", content)
        self.assertIn("return oldestMatching->instanceId;", content)

if __name__ == "__main__":
    unittest.main()
