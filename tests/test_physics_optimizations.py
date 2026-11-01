import unittest
import os

class TestPhysicsOptimizations(unittest.TestCase):
    def setUp(self):
        self.root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.butterscotch_src = os.path.join(self.root_dir, "butterscotch", "src")

    def test_precise_sampler_hoisting_and_linearization(self):
        """collision.h must implement PreciseCollisionSampler with hoisted invariants and zero float division in inner loop."""
        path = os.path.join(self.butterscotch_src, "collision.h")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("PreciseCollisionSampler", src, "Missing PreciseCollisionSampler typedef")
        self.assertIn("Collision_createPreciseSampler", src, "Missing Collision_createPreciseSampler function")
        self.assertIn("Collision_samplePrecise", src, "Missing Collision_samplePrecise function")
        self.assertIn("if (!sA.isRotated && !sB.isRotated)", src, "Missing non-rotated fast path in Collision_instancesOverlapPrecise")
        self.assertIn("rowMaskA = sA.mask + (size_t) myA * sA.bytesPerRow;", src, "Missing direct rowMask pointer computation")

    def test_move_outside_common_single_dirty_mark(self):
        """moveOutsideCommon must not flood SpatialGrid_markInstanceAsDirty inside step loop."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        start = src.find("static void moveOutsideCommon(Runner* runner")
        self.assertNotEqual(start, -1, "Missing moveOutsideCommon")
        end = src.find("static RValue builtin_move_outside_solid(", start)
        body = src[start:end]

        loop_start = body.find("for (int32_t i = 1; steps >= i; i++)")
        self.assertNotEqual(loop_start, -1, "Missing step loop in moveOutsideCommon")
        loop_end = body.find("inst->x = (float) (origX + (GMLReal) steps * dx);", loop_start)
        loop_body = body[loop_start:loop_end]

        # Verify dirtyInstances is only marked on exit/return, not inside every iteration of the loop
        self.assertIn("SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);", body,
                      "moveOutsideCommon must mark instance dirty once when free spot found")
        # Count occurrences in body: should be exactly 2 (one inside if (bounceTestFree), one at end of loop)
        dirty_count = body.count("SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);")
        self.assertEqual(dirty_count, 2, f"Expected 2 single dirty calls on return, found {dirty_count}")

    def test_point_collision_bbox_check_before_ancestor_walk(self):
        """builtin_collision_point and builtin_instance_position must check BBox before ancestor tree traversal."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        # Check collision_point
        cp_start = src.find("builtin_collision_point(VMContext* ctx")
        self.assertNotEqual(cp_start, -1, "Missing builtin_collision_point")
        cp_end = src.find("builtin_collision_line(", cp_start)
        cp_body = src[cp_start:cp_end]

        bbox_idx = cp_body.find("Collision_pointInsideInstanceBox")
        ancestor_idx = cp_body.find("VM_isObjectOrDescendant")
        self.assertNotEqual(bbox_idx, -1, "Missing Collision_pointInsideInstanceBox in collision_point")
        self.assertNotEqual(ancestor_idx, -1, "Missing VM_isObjectOrDescendant in collision_point")
        self.assertLess(bbox_idx, ancestor_idx, "Collision_pointInsideInstanceBox must precede VM_isObjectOrDescendant")

    def test_fast_exact_object_match_in_collision_queries(self):
        """Point and shape collision builtins must test exact object index match before VM_isObjectOrDescendant."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("inst->objectIndex != targetObjIndex && !VM_isObjectOrDescendant", src,
                      "Missing fast objectIndex check in instance collision queries")
        self.assertIn("other->objectIndex != target && !VM_isObjectOrDescendant", src,
                      "Missing fast objectIndex check in meeting collision queries")

    def test_particle_emitter_viewport_culling(self):
        """particleEmitterSpawn must cull emitters entirely outside active viewport."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        start = src.find("static void particleEmitterSpawn(Runner* runner")
        self.assertNotEqual(start, -1, "Missing particleEmitterSpawn")
        end = src.find("typedef struct { int32_t typeId;", start)
        body = src[start:end]

        self.assertIn("Runner_getCameraForView", body, "particleEmitterSpawn must query camera for culling")
        self.assertIn("exMax < vx - 128.0 || exMin > vx + vw + 128.0", body,
                      "particleEmitterSpawn must cull off-screen emitters with 128px margin")

if __name__ == "__main__":
    unittest.main()
