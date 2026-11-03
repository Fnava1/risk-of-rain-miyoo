import unittest
import os

class TestProvidenceTileAndSectorOptimizations(unittest.TestCase):
    def setUp(self):
        self.root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.runner_h = os.path.join(self.root, "butterscotch", "src", "runner.h")
        self.runner_c = os.path.join(self.root, "butterscotch", "src", "runner.c")
        self.native_subsystem_c = os.path.join(self.root, "butterscotch", "src", "native_level_subsystem.c")

    def test_tile_layer_map_lookup_hoisted_or_guarded(self):
        with open(self.runner_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()
        self.assertIn("hasTileLayerMap", src)
        self.assertIn("tileLayerMap != nullptr && hmlen(runner->tileLayerMap) > 0", src)

    def test_tile_frustum_integer_fastpath(self):
        with open(self.runner_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()
        self.assertIn("viewX0", src)
        self.assertIn("viewX1", src)
        self.assertIn("tile->x + (int32_t)tile->width < viewX0", src)

    def test_sector_hibernation_covers_ambient_cargo_instances(self):
        with open(self.native_subsystem_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()
        self.assertIn("oRainSplash", src)
        self.assertIn("is_cargo_ambient", src)
        self.assertIn("inst->solid", src)
        self.assertIn("sectorHibernated = true", src)

    def test_sector_tile_filtering_on_hibernation(self):
        with open(self.runner_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()
        self.assertIn("runner->sector_hibernation_active && tile->x < 4000", src)
        self.assertIn("cachedRoomTilesSectorHibernated", src)

        with open(self.runner_h, "r", encoding="utf-8", errors="ignore") as f:
            hdr = f.read()
        self.assertIn("bool cachedRoomTilesSectorHibernated;", hdr)

    def test_providence_hibernation_requires_battle_activation_not_just_door2(self):
        with open(self.native_subsystem_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()
        # Must inspect oCommand terminal and boss alive status before activating hibernation
        self.assertIn("oCommand", src)
        self.assertIn("oBoss1", src)
        self.assertIn("oBoss2Clone", src)
        self.assertIn("oBoss3", src)
        self.assertIn("battle_started", src)
        self.assertIn("in_providence_fight", src)

    def test_providence_hibernation_deactivation_and_wake_up(self):
        with open(self.native_subsystem_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()
        # When fight ends or player exits arena, hibernation deactivates and wakes up entities
        self.assertIn("runner->sector_hibernation_active && !in_providence_fight", src)
        self.assertIn("inst->active = true;", src)
        self.assertIn("inst->sectorHibernated = false;", src)

if __name__ == "__main__":
    unittest.main()
