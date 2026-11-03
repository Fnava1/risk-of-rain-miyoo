import unittest
import os

class TestSpiteWurmsNativeAcceleration(unittest.TestCase):
    def setUp(self):
        self.repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.native_subsystem_c = os.path.join(self.repo_root, "butterscotch", "src", "native_level_subsystem.c")
        self.native_subsystem_h = os.path.join(self.repo_root, "butterscotch", "src", "native_level_subsystem.h")
        self.vm_builtins_c = os.path.join(self.repo_root, "butterscotch", "src", "vm_builtins.c")

    def test_wurm_body_override_removed(self):
        """oWurmBody must NOT have an override in NCLP_overrideEvent to prevent folded bodies."""
        with open(self.native_subsystem_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        override_func = src[src.find("NCLP_overrideEvent("):]
        self.assertNotIn("s_obj_wurm_body >= 0 && inst->objectIndex == s_obj_wurm_body", override_func,
                         "oWurmBody override must be removed so oWurmHead_Step_2 and oWurmBody_Step_2 run canonically")

    def test_spite_grenade_native_acceleration(self):
        """oEfGrenadeEnemy must have native O(1) SGB bounce and friction handling."""
        with open(self.native_subsystem_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        override_func = src[src.find("NCLP_overrideEvent("):]
        self.assertIn("s_obj_ef_grenade_enemy >= 0 && inst->objectIndex == s_obj_ef_grenade_enemy", override_func)
        self.assertIn("inst->hspeed = -inst->hspeed * 0.8f", override_func)
        self.assertIn("inst->vspeed = -inst->vspeed * 0.6f", override_func)
        self.assertIn("Runner_destroyInstance(runner, inst, true)", override_func)

    def test_sound_mixer_voice_capping(self):
        """builtin_sound_play must include per-frame deduplication to prevent audio buffer saturation."""
        with open(self.vm_builtins_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        sp_func = src[src.find("builtin_sound_play("):src.find("builtin_sound_loop(")]
        self.assertIn("s_sound_counts", sp_func)
        self.assertIn("s_sound_counts[soundIndex] >= 3", sp_func)

    def test_collision_point_and_line_fast_paths(self):
        """collision_point and collision_line must fast-path pBlock (43) to SGB bitmask and DDA raycast."""
        with open(self.vm_builtins_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        self.assertIn("NCLP_checkSolid(runner, (float)px, (float)py, (float)px, (float)py)", src)
        self.assertIn("NCLP_raycastSolid(runner, (float)lx1, (float)ly1, (float)lx2, (float)ly2", src)

    def test_gold_and_missile_native_kinematics(self):
        """oEfGold and oEfMissile must have native kinematics hooks in NCLP_overrideEvent."""
        with open(self.native_subsystem_c, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        override_func = src[src.find("NCLP_overrideEvent("):]
        self.assertIn("s_obj_ef_gold >= 0 && inst->objectIndex == s_obj_ef_gold", override_func)
        self.assertIn("s_obj_ef_missile >= 0 && inst->objectIndex == s_obj_ef_missile", override_func)

if __name__ == '__main__':
    unittest.main()
