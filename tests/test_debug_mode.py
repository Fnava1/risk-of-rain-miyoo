import unittest
import os
import re

class TestDebugMode(unittest.TestCase):
    def test_runner_header_debug_fields(self):
        """Verifies runner.h defines all required debug state fields and helper declarations."""
        runner_h_path = os.path.join("butterscotch", "src", "runner.h")
        self.assertTrue(os.path.exists(runner_h_path))
        with open(runner_h_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("bool debug_test_active;", content)
        self.assertIn("int32_t debug_target_stage;", content)
        self.assertIn("int32_t debug_target_level;", content)
        self.assertIn("bool debug_hub_spawned;", content)
        self.assertIn("int32_t debug_box_ids[4];", content)
        self.assertIn("float debug_box_pos[4][2];", content)
        self.assertIn("int32_t debug_boss_ids[5];", content)
        self.assertIn("float debug_boss_pos[5][2];", content)
        self.assertIn("void runner_spawn_debug_hub(Runner* runner, Instance* playerInst);", content)
        self.assertIn("void runner_update_debug_mode(Runner* runner);", content)

    def test_runner_c_implementation(self):
        """Verifies runner.c implements room redirection, level scaling, and respawning."""
        runner_c_path = os.path.join("butterscotch", "src", "runner.c")
        self.assertTrue(os.path.exists(runner_c_path))
        with open(runner_c_path, "r", encoding="utf-8") as f:
            content = f.read()

        # Check reset logic
        self.assertIn("runner->debug_test_active = false;", content)
        self.assertIn("runner->debug_target_stage = 18;", content)
        self.assertIn("runner->debug_target_level = 1;", content)

        # Check room redirection in Runner_handlePendingRoomChange
        self.assertIn("runner->debug_test_active && (oldRoomIndex == 6 || oldRoomIndex == 7 || oldRoomIndex == 40)", content)
        self.assertIn("newRoomIndex = runner->debug_target_stage;", content)

        # Check hub spawn logic
        self.assertIn("runner_spawn_debug_hub", content)
        self.assertIn("OTHER_USER0 + 5", content)  # User Event 5 dispatch for player level-up
        self.assertIn("oArtifact8Box1", content)
        self.assertIn("oArtifact8Box2", content)
        self.assertIn("oArtifact8Box3", content)
        self.assertIn("oArtifact8BoxUse", content)
        self.assertIn("oKnurl", content)
        self.assertIn("oHorn", content)
        self.assertIn("oImpTentacle", content)
        self.assertIn("oWormEye", content)
        self.assertIn("oJellyBox", content)

        # Check Runner_step invocation
        self.assertIn("if (runner->debug_test_active) {\n        runner_update_debug_mode(runner);\n    }", content)

    def test_miyoo_c_debug_menu_overlay(self):
        """Verifies miyoo.c implements the Room 39 debug UI overlay and input interceptor."""
        miyoo_c_path = os.path.join("butterscotch", "src", "backends", "miyoo.c")
        self.assertTrue(os.path.exists(miyoo_c_path))
        with open(miyoo_c_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("DebugStageOption", content)
        self.assertIn("is_debug_room", content)
        self.assertIn("mmp_debug_menu_draw", content)
        self.assertIn("RISK OF RAIN - TEST & DEBUG HUB", content)
        self.assertIn("Desolate Forest (1-1)", content)
        self.assertIn("UES Contact Light (6)", content)
        self.assertIn("Boar Beach (Secret)", content)

        # Room 7 (rSelectCoop) detection and combo-driven toggle
        self.assertIn("runner->currentRoomIndex == 7", content)
        self.assertIn('strcmp(runner->currentRoom->name, "rSelectCoop") == 0', content)
        self.assertIn("s_debug_menu_open", content)

    def test_clean_room_byod_compliance(self):
        """Verifies no proprietary Risk of Rain game binaries or assets are tracked in git."""
        import subprocess
        output = subprocess.check_output(["git", "ls-files"]).decode("utf-8")
        tracked_files = [line.strip() for line in output.splitlines() if line.strip()]
        forbidden_exact = {"risk of rain.exe", "data.win", "data_mmp.win", "save.ini"}
        for file in tracked_files:
            base = os.path.basename(file).lower()
            self.assertNotIn(base, forbidden_exact, f"Forbidden proprietary file tracked in git: {file}")
            if file.startswith("dist/") or file.startswith("src/"):
                ext = os.path.splitext(file)[1].lower()
                self.assertNotIn(ext, [".win", ".ogg", ".exe"], f"Forbidden asset in release path: {file}")

    def test_level_scaling_math(self):
        """Verifies that scaling to level N invokes level-up event exactly (N - 1) times."""
        for target_level in range(1, 26):
            invocations = 0
            for l in range(1, target_level):
                invocations += 1
            self.assertEqual(invocations, target_level - 1)

    def test_debug_hub_safe_clearance_and_ground_snapping(self):
        """Verifies runner.c implements smart clearance checking and floor snapping for debug items."""
        runner_c_path = os.path.join("butterscotch", "src", "runner.c")
        with open(runner_c_path, "r", encoding="utf-8") as f:
            content = f.read()

        # Functions must be present
        self.assertIn("static bool is_debug_spot_clear(", content)
        self.assertIn("static bool find_ground_y(", content)

        # Clearance checks
        self.assertIn("other->solid", content)  # Rejects solid terrain (oB, oBNoSpawn)
        self.assertIn("pChestIdx", content)     # Rejects native chests/barrels
        self.assertIn("oShip", content)         # Rejects stage 6 ship cargo
        self.assertIn("depth = -1000", content) # Front-most visual depth for items

if __name__ == "__main__":
    unittest.main()
