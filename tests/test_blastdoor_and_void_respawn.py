"""
tests/test_blastdoor_and_void_respawn.py
Regression tests for Level 6 blast door collision desync and void fall respawn fixes.
"""

import os
import re
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

class TestBlastdoorAndVoidRespawnFixes(unittest.TestCase):

    def test_nclp_clear_solid_implemented(self):
        """Verify NCLP_clearSolid is declared in header and implemented in native_level_subsystem.c."""
        header_path = os.path.join(REPO_ROOT, "butterscotch", "src", "native_level_subsystem.h")
        with open(header_path, "r", encoding="utf-8") as f:
            header = f.read()
        self.assertIn("void NCLP_clearSolid(", header)

        src_path = os.path.join(REPO_ROOT, "butterscotch", "src", "native_level_subsystem.c")
        with open(src_path, "r", encoding="utf-8") as f:
            src = f.read()
        self.assertIn("void NCLP_clearSolid(Runner* runner", src)
        self.assertIn("runner->room_solid_bitmask[bitIdx / 8] &= (uint8_t)~(1u << (bitIdx % 8));", src)

    def test_runner_destroy_instance_preserves_solid_bitmask(self):
        """Verify Runner_destroyInstance does NOT clear bitmask on block destruction so merged blocks remain solid."""
        runner_path = os.path.join(REPO_ROOT, "butterscotch", "src", "runner.c")
        with open(runner_path, "r", encoding="utf-8") as f:
            src = f.read()
        func_start = src.find("void Runner_destroyInstance(")
        func_end = src.find("void Runner_destroyInstancesMatching(", func_start)
        func_body = src[func_start:func_end]
        self.assertNotIn("NCLP_clearSolid", func_body)

    def test_no_fake_100001_instance_ids(self):
        """Verify no collision builtins return dummy constant 100001."""
        builtins_path = os.path.join(REPO_ROOT, "butterscotch", "src", "vm_builtins.c")
        with open(builtins_path, "r", encoding="utf-8") as f:
            builtins = f.read()
        self.assertNotIn("100001", builtins, "Found forbidden fake instance ID 100001 in vm_builtins.c!")

    def test_outside_view_separated_from_outside_room(self):
        """Verify outsideViewMask is used in dispatchOutsideViewEvents and outsideRoom is reset."""
        inst_h = os.path.join(REPO_ROOT, "butterscotch", "src", "instance.h")
        with open(inst_h, "r", encoding="utf-8") as f:
            hdr = f.read()
        self.assertIn("uint8_t outsideViewMask;", hdr)

        runner_c = os.path.join(REPO_ROOT, "butterscotch", "src", "runner.c")
        with open(runner_c, "r", encoding="utf-8") as f:
            src = f.read()
        self.assertIn("inst->outsideViewMask |= viewBit;", src)
        self.assertIn("inst->outsideViewMask &= (uint8_t)~viewBit;", src)
        self.assertIn("inst->outsideRoom = true;", src)
        self.assertIn("inst->outsideRoom = false;", src)

    def test_precover_fallback_in_instance_nearest(self):
        """Verify instance_nearest has pRecover (46) fallback to oTeleporterFake (65)."""
        builtins_path = os.path.join(REPO_ROOT, "butterscotch", "src", "vm_builtins.c")
        with open(builtins_path, "r", encoding="utf-8") as f:
            builtins = f.read()
        self.assertIn("objectIndex == 46 && 65 < (int32_t)runner->dataWin->objt.count", builtins)
        self.assertIn("fakeTps = runner->instancesByExactObject[65]", builtins)

if __name__ == "__main__":
    unittest.main()
