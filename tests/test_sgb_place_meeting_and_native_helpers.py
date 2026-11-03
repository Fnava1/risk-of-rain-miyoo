import os
import unittest


class TestSgbPlaceMeetingAndNativeHelpers(unittest.TestCase):
    def setUp(self):
        self.repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.butterscotch_src = os.path.join(self.repo_root, "butterscotch", "src")
        self.docs_dir = os.path.join(self.repo_root, "docs")

    def test_native_boundaries_doc_exists(self):
        """Permanent documentation for forbidden native optimization boundaries must exist."""
        doc_path = os.path.join(self.docs_dir, "native_optimization_boundaries.md")
        self.assertTrue(os.path.exists(doc_path), f"Missing {doc_path}")
        with open(doc_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()

        self.assertIn("step_player", content)
        self.assertIn("oDirectorControl", content)
        self.assertIn("use_item", content)
        self.assertIn("DO NOT TOUCH", content)

    def test_spatial_grid_preservation_in_place_meeting(self):
        """builtin_place_meeting must query SpatialGrid directly without premature bitmask early exit to preserve merged block solidity."""
        vm_builtins = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(vm_builtins, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static RValue builtin_place_meeting(")
        self.assertNotEqual(func_start, -1, "Missing builtin_place_meeting")
        func_end = src.find("static inline GMLReal compatRoundCoord(", func_start)
        func_body = src[func_start:func_end]

        # Must NOT early-exit on bitmask check
        self.assertNotIn("!NCLP_checkSolid", func_body)
        self.assertIn("SpatialGrid_syncGrid(runner, runner->spatialGrid);", func_body)

    def test_spatial_grid_preservation_in_instance_place(self):
        """builtin_instance_place must query SpatialGrid directly without premature bitmask early exit."""
        vm_builtins = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(vm_builtins, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static RValue builtin_instance_place(")
        self.assertNotEqual(func_start, -1, "Missing builtin_instance_place")
        func_end = src.find("static RValue builtin_instance_place_list(", func_start)
        func_body = src[func_start:func_end]

        # Must NOT early-exit on bitmask check
        self.assertNotIn("!NCLP_checkSolid", func_body)
        self.assertIn("SpatialGrid_syncGrid(runner, runner->spatialGrid);", func_body)

    def test_allied_drones_canonical_gml_preservation(self):
        """native_level_subsystem.c must not run unbounded 100-cell SpatialGrid scans for pDrone every frame."""
        nclp_path = os.path.join(self.butterscotch_src, "native_level_subsystem.c")
        with open(nclp_path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertNotIn("SpatialGrid_prepareQuery(runner, inst->x - 250.0, inst->y - 250.0", src)

    def test_sector_hibernation_enemy_restriction(self):
        """Spatial Sector Hibernation must only hibernate non-boss enemies and latch past Door 2 (x >= 4150)."""
        nclp_path = os.path.join(self.butterscotch_src, "native_level_subsystem.c")
        with open(nclp_path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("bool in_providence_arena = (p->x >= 4150.0f);", src)
        self.assertIn("if (!runner->isEnemyObject || !runner->isEnemyObject[inst->objectIndex] || (runner->isBossObject && runner->isBossObject[inst->objectIndex])) continue;", src)
        self.assertIn("if (inst->solid) continue;", src)

    def test_enemy_and_boss_classification_populated(self):
        """runner.c must populate isEnemyObject and isBossObject using RoR canonical names."""
        runner_c = os.path.join(self.butterscotch_src, "runner.c")
        with open(runner_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("isRoREnemyName(oname)", src)
        self.assertIn("isRoRBossName(oname)", src)
        self.assertIn("runner->isEnemyObject[oi] = true;", src)
        self.assertIn("runner->isBossObject[oi] = true;", src)

    def test_string_builtins_zero_alloc_fastpaths(self):
        """String builtins must use resolveTextArg to avoid heap malloc churn during chest item selection."""
        vm_builtins = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(vm_builtins, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("static inline const char* resolveTextArg(RValue val, DataWin* dataWin, char** outOwned)", src)
        self.assertIn("resolveTextArg(args[0], ctx->runner->dataWin, &ownedStr);", src)

    def test_experience_orbs_alarm_1_kinematics(self):
        """native_level_subsystem.c must handle oEfExp Alarm 1 with native vector kinematics."""
        nclp_path = os.path.join(self.butterscotch_src, "native_level_subsystem.c")
        with open(nclp_path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("s_obj_ef_exp >= 0 && inst->objectIndex == s_obj_ef_exp", src)
        self.assertIn("eventType == EVENT_ALARM && eventSubtype == 1", src)
        self.assertIn("inst->alarm[1] = 1;", src)
        self.assertIn("atan2f(-dy, dx)", src)
        self.assertIn("inst->speed = fminf(inst->speed + 0.35f, 10.0f);", src)

    def test_runner_drone_table_lifecycle(self):
        """Runner must declare, allocate and free isDroneObject table."""
        runner_h = os.path.join(self.butterscotch_src, "runner.h")
        with open(runner_h, "r", encoding="utf-8", errors="ignore") as f:
            h_src = f.read()
        self.assertIn("bool* isDroneObject;", h_src)

        runner_c = os.path.join(self.butterscotch_src, "runner.c")
        with open(runner_c, "r", encoding="utf-8", errors="ignore") as f:
            c_src = f.read()
        self.assertIn("runner->isDroneObject = (bool*) safeCalloc(dataWin->objt.count, sizeof(bool));", c_src)
        self.assertIn("runner->isDroneObject[oi] = true;", c_src)
        self.assertIn("free(runner->isDroneObject);", c_src)


if __name__ == "__main__":
    unittest.main()
