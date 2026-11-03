import unittest
import os

class TestLevel6Optimizations(unittest.TestCase):
    def setUp(self):
        self.root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.butterscotch_src = os.path.join(self.root_dir, "butterscotch", "src")

    def test_builtin_collision_line_uses_spatial_grid(self):
        """builtin_collision_line must query SpatialGrid with line AABB instead of linear scanning all solid blocks."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("builtin_collision_line(VMContext* ctx")
        self.assertNotEqual(func_start, -1, "Missing builtin_collision_line")
        func_end = src.find("builtin_collision_rectangle(", func_start)
        func_body = src[func_start:func_end]

        self.assertIn("SpatialGrid_syncGrid", func_body,
                      "builtin_collision_line must sync spatial grid")
        self.assertIn("SpatialGrid_prepareQuery", func_body,
                      "builtin_collision_line must use SpatialGrid_prepareQuery with line AABB")
        self.assertIn("GMLReal bx1 = GMLReal_fmin(lx1, lx2);", func_body,
                      "builtin_collision_line must compute line AABB bx1")
        self.assertIn("GMLReal bx2 = GMLReal_fmax(lx1, lx2);", func_body,
                      "builtin_collision_line must compute line AABB bx2")

    def test_collision_point_and_instance_position_use_spatial_grid(self):
        """builtin_collision_point and builtin_instance_position must use SpatialGrid."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        # collision_point
        cp_start = src.find("builtin_collision_point(VMContext* ctx")
        self.assertNotEqual(cp_start, -1, "Missing builtin_collision_point")
        cp_end = src.find("builtin_collision_line(", cp_start)
        cp_body = src[cp_start:cp_end]
        self.assertIn("SpatialGrid_prepareQuery", cp_body)

        # instance_position
        ip_start = src.find("builtin_instance_position(VMContext* ctx")
        self.assertNotEqual(ip_start, -1, "Missing builtin_instance_position")
        ip_end = src.find("static RValue builtin_instance_place(", ip_start)
        ip_body = src[ip_start:ip_end]
        self.assertIn("SpatialGrid_prepareQuery", ip_body)

    def test_runner_debug_draw_markers_culled(self):
        """Runner must cull invisible debug marker objects (oB, oBNoSpawn, oRope) during draw."""
        path = os.path.join(self.butterscotch_src, "runner.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("isDebugMarkerObject", src,
                      "runner.c must declare isDebugMarkerObject")
        self.assertIn("isDebugMarkerObject(runner, inst->objectIndex)", src,
                      "drawInstanceNormally must skip debug marker objects when debug is inactive")

    def test_runner_decoupled_tile_sorting(self):
        """rebuildDrawableCacheIfDirty must cache pre-sorted room tiles and merge with sorted dynamic drawables."""
        path = os.path.join(self.butterscotch_src, "runner.c")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("cachedRoomTiles", src,
                      "runner.c must use cachedRoomTiles for room tile decoupling")
        self.assertIn("cachedDynamicDrawables", src,
                      "runner.c must maintain cachedDynamicDrawables")
        self.assertIn("qsort(runner->cachedDynamicDrawables", src,
                      "runner.c must sort only dynamic drawables")

    def test_runner_no_mid_draw_qsort_abort(self):
        """Runner_draw must protect draw loop with inDrawLoop and avoid mid-draw abort / restart loops."""
        path = os.path.join(self.butterscotch_src, "runner.c")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        draw_start = src.find("void Runner_draw(Runner* runner)")
        self.assertNotEqual(draw_start, -1, "Missing Runner_draw")
        draw_end = src.find("static void cleanupState(Runner* runner)", draw_start)
        if draw_end == -1:
            draw_end = draw_start + 2000
        draw_body = src[draw_start:draw_end]

        self.assertIn("runner->inDrawLoop = true;", draw_body)
        self.assertIn("runner->inDrawLoop = false;", draw_body)
        self.assertNotIn("while (true)", draw_body,
                         "Runner_draw must not contain mid-draw restarting while (true) loop")

    def test_spatial_grid_remove_instance_o1_early_exit(self):
        """SpatialGrid_removeInstance must return O(1) immediately when collisionCells is empty, with no fallback loop."""
        path = os.path.join(self.butterscotch_src, "spatial_grid.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("void SpatialGrid_removeInstance(")
        self.assertNotEqual(func_start, -1, "Missing SpatialGrid_removeInstance")
        func_end = src.find("void SpatialGrid_syncGrid(", func_start)
        func_body = src[func_start:func_end]

        self.assertIn("if (cellCount == 0)", func_body,
                      "SpatialGrid_removeInstance must check for empty collisionCells")
        self.assertNotIn("if (!removedAny && instance->destroyed)", func_body,
                         "SpatialGrid_removeInstance must not contain slow brute-force scan over all cells")

    def test_particle_spawn_viewport_culling(self):
        """particleSpawnAt must cull particles spawned outside camera view + 128px margin on handhelds."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static bool particleSpawnAt(")
        self.assertNotEqual(func_start, -1, "Missing particleSpawnAt")
        func_end = src.find("static void particleEmitterPoint(", func_start)
        func_body = src[func_start:func_end]

        self.assertIn("if (runner->viewsEnabled)", func_body,
                      "particleSpawnAt must check if views are enabled")
        self.assertIn("Runner_getCameraForView(runner, 0)", func_body,
                      "particleSpawnAt must get view 0 camera")
        self.assertIn("x < vx - 128.0", func_body,
                      "particleSpawnAt must cull x < vx - 128.0")
        self.assertIn("return true;", func_body,
                      "particleSpawnAt must return true when culled to avoid caller early break")

    def test_move_contact_solid_standard_gms_compliance(self):
        """moveContactCommon must comply with standard GM:S 1.4: never artificially teleport instances upwards."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static void moveContactCommon(")
        self.assertNotEqual(func_start, -1, "Missing moveContactCommon")
        func_end = src.find("static RValue builtin_move_contact_solid(", func_start)
        func_body = src[func_start:func_end]

        self.assertIn("Standard GameMaker Studio 1.4 behavior", func_body,
                      "moveContactCommon must document GM:S 1.4 standard behavior")
        self.assertNotIn("inst->y -= (float) up;", func_body,
                         "moveContactCommon must never artificially move instances upwards")
        self.assertNotIn("for (int32_t up = 16;", func_body,
                         "moveContactCommon must not contain unhang loop")


if __name__ == "__main__":
    unittest.main()
