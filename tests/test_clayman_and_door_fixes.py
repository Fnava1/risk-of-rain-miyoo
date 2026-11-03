import os
import unittest

class TestDoorLevitationFix(unittest.TestCase):
    def setUp(self):
        self.root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.vm_builtins = os.path.join(self.root_dir, "butterscotch", "src", "vm_builtins.c")
        self.assertTrue(os.path.exists(self.vm_builtins))

    def test_door_levitation_restricted_to_chests_and_barrels(self):
        """moveContactCommon must never artificially elevate entities upwards, preventing door edge levitation and floating objects."""
        with open(self.vm_builtins, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static void moveContactCommon(")
        self.assertNotEqual(func_start, -1)
        func_end = src.find("static RValue builtin_move_contact_solid(", func_start)
        func_body = src[func_start:func_end]

        self.assertNotIn("inst->y -= (float) up;", func_body)
        self.assertIn("Standard GameMaker Studio 1.4 behavior: if an instance is already in collision with solid geometry,", func_body)
        self.assertIn("return;", func_body)

if __name__ == "__main__":
    unittest.main()
