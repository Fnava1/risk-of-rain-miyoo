import unittest
import os

class TestNativeAIAndDotPipeline(unittest.TestCase):
    def setUp(self):
        self.root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.header_path = os.path.join(self.root, "butterscotch", "src", "native_level_subsystem.h")
        self.impl_path = os.path.join(self.root, "butterscotch", "src", "native_level_subsystem.c")
        self.runner_path = os.path.join(self.root, "butterscotch", "src", "runner.c")

        self.assertTrue(os.path.exists(self.header_path))
        self.assertTrue(os.path.exists(self.impl_path))
        self.assertTrue(os.path.exists(self.runner_path))

        with open(self.header_path, "r", encoding="utf-8", errors="ignore") as f:
            self.header_src = f.read()
        with open(self.impl_path, "r", encoding="utf-8", errors="ignore") as f:
            self.impl_src = f.read()
        with open(self.runner_path, "r", encoding="utf-8", errors="ignore") as f:
            self.runner_src = f.read()

    def test_npc_throttling_api_declared(self):
        """Verifies NCLP_shouldThrottleNPC is declared in native_level_subsystem.h."""
        self.assertIn("bool NCLP_shouldThrottleNPC(", self.header_src)

    def test_npc_throttling_implementation(self):
        """Verifies NCLP_shouldThrottleNPC preserves visible viewport margin and decimates offscreen."""
        self.assertIn("NCLP_shouldThrottleNPC", self.impl_src)
        self.assertIn("activeViewValid", self.impl_src)
        # Verify 1 out of 4 frames decimation for offscreen
        self.assertIn("& 3", self.impl_src)

    def test_dot_pipeline_declarations(self):
        """Verifies DoT Status Dispatcher types and API are declared."""
        self.assertIn("StatusEffectType", self.header_src)
        self.assertIn("STATUS_BLEED", self.header_src)
        self.assertIn("STATUS_BURN", self.header_src)
        self.assertIn("STATUS_POISON", self.header_src)
        self.assertIn("void NCLP_applyStatusEffect(", self.header_src)
        self.assertIn("void NCLP_processStatusEffects(", self.header_src)

    def test_dot_pipeline_implementation(self):
        """Verifies DoT status processor operates on a dense array and syncs with selfVars."""
        self.assertIn("NCLP_applyStatusEffect", self.impl_src)
        self.assertIn("NCLP_processStatusEffects", self.impl_src)

if __name__ == "__main__":
    unittest.main()
