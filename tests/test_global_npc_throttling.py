import unittest
import os

class TestGlobalNPCThrottling(unittest.TestCase):
    def setUp(self):
        self.repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.runner_c = os.path.join(self.repo_root, "butterscotch", "src", "runner.c")
        self.runner_h = os.path.join(self.repo_root, "butterscotch", "src", "runner.h")
        self.native_subsystem = os.path.join(self.repo_root, "butterscotch", "src", "native_level_subsystem.c")

    def test_runner_classifies_enemy_hierarchy(self):
        """Runner_create must allocate and populate isEnemyObject and isBossObject arrays."""
        with open(self.runner_h, "r", encoding="utf-8", errors="ignore") as f:
            hdr = f.read()
        self.assertIn("bool* isEnemyObject;", hdr)
        self.assertIn("bool* isBossObject;", hdr)

        with open(self.runner_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("runner->isEnemyObject = (bool*) safeCalloc(dataWin->objt.count, sizeof(bool));", src)
        self.assertIn("runner->isBossObject = (bool*) safeCalloc(dataWin->objt.count, sizeof(bool));", src)
        self.assertIn("strcmp(dataWin->objt.objects[oi].name, \"pEnemy\") == 0", src)
        self.assertIn("strcmp(dataWin->objt.objects[oi].name, \"pBoss\") == 0", src)
        self.assertIn("free(runner->isEnemyObject);", src)
        self.assertIn("free(runner->isBossObject);", src)

    def test_runner_throttles_only_regular_enemies(self):
        """Runner_executeResolvedEvent must only decimate STEP_NORMAL for off-screen non-boss enemies."""
        with open(self.runner_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("if (eventType == EVENT_STEP && eventSubtype == STEP_NORMAL)", src)
        self.assertIn("runner->isEnemyObject[instance->objectIndex]", src)
        self.assertIn("!runner->isBossObject[instance->objectIndex]", src)
        self.assertIn("NCLP_shouldThrottleNPC(runner, instance)", src)

    def test_drone_dispenser_canonical_gml(self):
        """Drones must maintain canonical GML lifecycle without unbounded spatial scans."""
        with open(self.native_subsystem, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertNotIn("SpatialGrid_prepareQuery(runner, inst->x - 250.0", src)

if __name__ == "__main__":
    unittest.main()
