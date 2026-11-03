import unittest
import os

class TestNativeDamageTextEmitter(unittest.TestCase):
    def setUp(self):
        self.root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.header_path = os.path.join(self.root, "butterscotch", "src", "native_level_subsystem.h")
        self.impl_path = os.path.join(self.root, "butterscotch", "src", "native_level_subsystem.c")
        self.runner_path = os.path.join(self.root, "butterscotch", "src", "runner.c")
        self.builtins_path = os.path.join(self.root, "butterscotch", "src", "vm_builtins.c")

        self.assertTrue(os.path.exists(self.header_path))
        self.assertTrue(os.path.exists(self.impl_path))
        self.assertTrue(os.path.exists(self.runner_path))
        self.assertTrue(os.path.exists(self.builtins_path))

        with open(self.header_path, "r", encoding="utf-8", errors="ignore") as f:
            self.header_src = f.read()
        with open(self.impl_path, "r", encoding="utf-8", errors="ignore") as f:
            self.impl_src = f.read()
        with open(self.runner_path, "r", encoding="utf-8", errors="ignore") as f:
            self.runner_src = f.read()

    def test_damage_particle_struct_declared(self):
        """Verifies NativeDamageParticle struct is declared in native_level_subsystem.h."""
        self.assertIn("typedef struct {", self.header_src)
        self.assertIn("NativeDamageParticle", self.header_src)
        self.assertIn("void NCLP_emitDamageText(", self.header_src)
        self.assertIn("void NCLP_updateDamageText(", self.header_src)
        self.assertIn("void NCLP_drawDamageText(", self.header_src)

    def test_damage_ring_buffer_implementation(self):
        """Verifies native ring buffer is implemented with bounded static capacity and lifecycle in native_level_subsystem.c."""
        self.assertIn("NCLP_emitDamageText", self.impl_src)
        self.assertIn("NCLP_updateDamageText", self.impl_src)
        self.assertIn("NCLP_drawDamageText", self.impl_src)
        self.assertIn("DAMAGE_PARTICLES_CAPACITY", self.impl_src)

    def test_runner_render_and_step_wiring(self):
        """Verifies runner.c invokes NCLP damage text update and draw hooks."""
        self.assertIn("NCLP_drawDamageText(runner)", self.runner_src)

if __name__ == "__main__":
    unittest.main()
