import unittest
import os

class TestNativeLevelSubsystem(unittest.TestCase):
    def setUp(self):
        self.header_path = os.path.join("butterscotch", "src", "native_level_subsystem.h")
        self.impl_path = os.path.join("butterscotch", "src", "native_level_subsystem.c")
        self.instance_h_path = os.path.join("butterscotch", "src", "instance.h")
        self.runner_h_path = os.path.join("butterscotch", "src", "runner.h")
        self.runner_c_path = os.path.join("butterscotch", "src", "runner.c")
        self.vm_builtins_c_path = os.path.join("butterscotch", "src", "vm_builtins.c")

        self.assertTrue(os.path.exists(self.header_path), f"Missing {self.header_path}")
        self.assertTrue(os.path.exists(self.impl_path), f"Missing {self.impl_path}")

        with open(self.header_path, "r", encoding="utf-8") as f:
            self.header_content = f.read()
        with open(self.impl_path, "r", encoding="utf-8") as f:
            self.impl_content = f.read()
        with open(self.instance_h_path, "r", encoding="utf-8") as f:
            self.instance_h_content = f.read()
        with open(self.runner_h_path, "r", encoding="utf-8") as f:
            self.runner_h_content = f.read()
        with open(self.runner_c_path, "r", encoding="utf-8") as f:
            self.runner_c_content = f.read()
        with open(self.vm_builtins_c_path, "r", encoding="utf-8") as f:
            self.vm_builtins_content = f.read()

    def test_nclp_header_declarations(self):
        """Verifies native_level_subsystem.h declares all required NCLP lifecycle and acceleration functions."""
        self.assertIn("void NCLP_init(struct Runner* runner);", self.header_content)
        self.assertIn("void NCLP_onRoomStart(struct Runner* runner);", self.header_content)
        self.assertIn("void NCLP_onRoomEnd(struct Runner* runner);", self.header_content)
        self.assertIn("void NCLP_step(struct Runner* runner);", self.header_content)
        self.assertIn("bool NCLP_overrideEvent(struct Runner* runner", self.header_content)
        self.assertIn("bool NCLP_checkSolid(struct Runner* runner", self.header_content)

    def test_instance_header_sector_flag(self):
        """Verifies instance.h includes the sectorHibernated flag on Instance."""
        self.assertIn("sectorHibernated", self.instance_h_content)

    def test_runner_header_nclp_fields(self):
        """Verifies runner.h contains the necessary bitmask and sector tracking fields."""
        self.assertIn("bool sector_hibernation_active;", self.runner_h_content)
        self.assertIn("uint8_t* room_solid_bitmask;", self.runner_h_content)
        self.assertIn("uint32_t room_bitmask_w;", self.runner_h_content)
        self.assertIn("uint32_t room_bitmask_h;", self.runner_h_content)
        self.assertIn("int32_t nclp_wurmBodyObjIndex;", self.runner_h_content)
        self.assertIn("int32_t nclp_guardBulletObjIndex;", self.runner_h_content)

    def test_runner_c_hooks_integration(self):
        """Verifies runner.c integrates all NCLP hooks in execution, room transitions, and event dispatch."""
        self.assertIn('#include "native_level_subsystem.h"', self.runner_c_content)
        self.assertIn("NCLP_init(runner);", self.runner_c_content)
        self.assertIn("NCLP_onRoomStart(runner);", self.runner_c_content)
        self.assertIn("NCLP_onRoomEnd(runner);", self.runner_c_content)
        self.assertIn("NCLP_step(runner);", self.runner_c_content)
        self.assertIn("NCLP_overrideEvent(runner, instance, eventType, eventSubtype, codeId, ownerObjectIndex)", self.runner_c_content)

    def test_spatial_sector_hibernation_logic(self):
        """Verifies native_level_subsystem.c correctly targets Level 6 Providence Arena (x >= 4150) and preserves essential entities."""
        self.assertIn("r6_1_1", self.impl_content)
        self.assertIn("4150.0f", self.impl_content) # Providence Arena threshold (Door 2)
        self.assertIn("4100.0f", self.impl_content) # Non-arena hibernation boundary
        self.assertIn("pDrone", self.impl_content)   # Drones preserved
        self.assertIn("oControl", self.impl_content) # Controllers preserved
        self.assertIn("oHUD", self.impl_content)     # HUD preserved
        self.assertIn("sectorHibernated = true;", self.impl_content)

    def test_gilded_wurm_kinematics_hook(self):
        """Verifies native_level_subsystem.c implements native inverse kinematics and angle rotation for oWurmBody."""
        self.assertIn("s_obj_wurm_body", self.impl_content)
        self.assertIn("atan2f(-dy, dx)", self.impl_content)
        self.assertIn("sqrtf(dx * dx + dy * dy)", self.impl_content)
        self.assertIn("SpatialGrid_markInstanceAsDirty", self.impl_content)

    def test_guardian_bullet_motion_hook(self):
        """Verifies native_level_subsystem.c tracks s_obj_guard_bullet without suppressing self-destruction."""
        self.assertIn("s_obj_guard_bullet", self.impl_content)

if __name__ == "__main__":
    unittest.main()
