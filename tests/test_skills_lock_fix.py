import unittest
import os
import re

class TestSkillsLockFix(unittest.TestCase):
    """
    Validates the fix for the skills permanent lock-out bug.
    In Risk of Rain, skills 2, 3, 4 check `alarm[3..5] == -1` to be cast.
    Wicked Ring (skull_ring) critical hits execute:
        parent.alarm[3] = max(0, parent.alarm[3] - 60 * skull_ring);
        parent.alarm[4] = max(0, parent.alarm[4] - 60 * skull_ring);
        parent.alarm[5] = max(0, parent.alarm[5] - 60 * skull_ring);
    If an alarm was ready (-1) or had <= 60 frames, max(0, ...) evaluated to 0.
    In prior versions, setting an alarm to 0 cleared its activeAlarmMask bit,
    permanently stranding the alarm at 0 and disabling skills 2, 3, 4 for the entire run
    while basic attack (alarm[2]) continued functioning.
    Additionally, the Hourglass use item executes `alarm[2..5] = 0;`, and CDR stacking
    can reduce cooldowns to 0.

    The fix ensures:
    1. Setting alarm <= 0 normalizes to -1 (inactive / ready) and clears activeAlarmMask.
    2. Reading alarm <= 0 evaluates as -1.0 (ready), making skill checks immune to locking.
    3. Runner alarm countdown checks <= 0, resetting to -1 and clearing mask without underflow.
    """

    def setUp(self):
        self.root_dir = os.path.dirname(os.path.dirname(__file__))
        self.vm_builtins_path = os.path.join(self.root_dir, 'butterscotch', 'src', 'vm_builtins.c')
        self.runner_path = os.path.join(self.root_dir, 'butterscotch', 'src', 'runner.c')

    def test_source_code_alarm_getter_normalizes_to_minus_one(self):
        """Verify vm_builtins.c getter returns -1.0 for any non-positive alarm."""
        self.assertTrue(os.path.exists(self.vm_builtins_path), "vm_builtins.c not found")
        with open(self.vm_builtins_path, 'r', encoding='utf-8', errors='ignore') as f:
            src = f.read()

        # Check BUILTIN_VAR_ALARM getter
        var_get_match = re.search(
            r'case BUILTIN_VAR_ALARM:\s*\{[\s\S]*?int32_t a = inst->alarm\[arrayIndex\];\s*if\s*\(a\s*<=\s*0\)\s*return RValue_makeReal\(-1\.0\);[\s\S]*?return RValue_makeReal\(\(GMLReal\)\s*a\);',
            src
        )
        self.assertIsNotNone(var_get_match, "BUILTIN_VAR_ALARM getter must return -1.0 for a <= 0")

        # Check builtin_alarm_get
        fn_get_match = re.search(
            r'builtin_alarm_get\([\s\S]*?int32_t a = inst->alarm\[alarmIndex\];\s*if\s*\(a\s*<=\s*0\)\s*return RValue_makeReal\(-1\.0\);[\s\S]*?return RValue_makeReal\(\(GMLReal\)\s*a\);',
            src
        )
        self.assertIsNotNone(fn_get_match, "builtin_alarm_get must return -1.0 for a <= 0")

    def test_source_code_runner_countdown_checks_lte_zero(self):
        """Verify runner.c checks inst->alarm[alarmIdx] <= 0 on countdown."""
        self.assertTrue(os.path.exists(self.runner_path), "runner.c not found")
        with open(self.runner_path, 'r', encoding='utf-8', errors='ignore') as f:
            src = f.read()

        runner_match = re.search(
            r'inst->alarm\[alarmIdx\]--;\s*if\s*\(inst->alarm\[alarmIdx\]\s*<=\s*0\)\s*\{\s*inst->alarm\[alarmIdx\]\s*=\s*-1;\s*inst->activeAlarmMask\s*&=',
            src
        )
        self.assertIsNotNone(runner_match, "runner.c must check alarm <= 0 to prevent negative underflow")

    def test_wicked_ring_crit_proc_simulation(self):
        """Simulate Wicked Ring crit procs on idle and near-ready skills."""
        class MockInstance:
            def __init__(self):
                self.alarm = [-1] * 12
                self.activeAlarmMask = 0

            def set_alarm(self, idx, val):
                # Matching GMS / Butterscotch setter fix
                new_val = int(round(val)) if isinstance(val, float) else int(val)
                if new_val <= 0:
                    new_val = -1
                self.alarm[idx] = new_val
                if new_val > 0:
                    self.activeAlarmMask |= (1 << idx)
                else:
                    self.activeAlarmMask &= ~(1 << idx)

            def get_alarm(self, idx):
                # Matching GMS / Butterscotch getter fix
                val = self.alarm[idx]
                if val <= 0:
                    return -1
                return val

            def can_cast_skill(self, skill_idx):
                # Skills 1..4 map to alarm 2..5
                alarm_idx = skill_idx + 1
                return self.get_alarm(alarm_idx) == -1

        inst = MockInstance()

        # All skills initially ready
        for s in range(1, 5):
            self.assertTrue(inst.can_cast_skill(s), f"Skill {s} should be ready initially")

        # Player fires basic attack (Skill 1), crits with Wicked Ring (skull_ring = 1)
        # oBullet_Step_1 runs:
        skull_ring = 1
        for a in [3, 4, 5]:
            inst.set_alarm(a, max(0, inst.get_alarm(a) - 60 * skull_ring))

        # Skills 2, 3, 4 MUST still be ready!
        for s in range(1, 5):
            self.assertTrue(inst.can_cast_skill(s), f"Skill {s} must remain ready after Wicked Ring crit")
            self.assertEqual(inst.get_alarm(s + 1), -1, f"Alarm {s + 1} must evaluate to -1")

        # Now put Skill 2 (alarm 3) on a short cooldown: 45 frames left
        inst.set_alarm(3, 45)
        self.assertFalse(inst.can_cast_skill(2), "Skill 2 on 45 frame cooldown must not be ready")
        self.assertEqual(inst.get_alarm(3), 45)

        # Wicked Ring crits again, reducing cooldown by 60 frames (45 - 60 = -15 -> max(0, -15) = 0)
        inst.set_alarm(3, max(0, inst.get_alarm(3) - 60 * skull_ring))

        # Skill 2 cooldown is now fully cleared; must be ready!
        self.assertTrue(inst.can_cast_skill(2), "Skill 2 must become ready after Wicked Ring cooldown clear")
        self.assertEqual(inst.get_alarm(3), -1)

    def test_hourglass_and_cdr_cooldown_reset_simulation(self):
        """Simulate Hourglass use item (alarm[2..5] = 0) and 100% CDR."""
        class MockInstance:
            def __init__(self):
                self.alarm = [-1] * 12
                self.activeAlarmMask = 0

            def set_alarm(self, idx, val):
                new_val = int(round(val)) if isinstance(val, float) else int(val)
                if new_val <= 0:
                    new_val = -1
                self.alarm[idx] = new_val
                if new_val > 0:
                    self.activeAlarmMask |= (1 << idx)
                else:
                    self.activeAlarmMask &= ~(1 << idx)

            def get_alarm(self, idx):
                val = self.alarm[idx]
                if val <= 0:
                    return -1
                return val

        inst = MockInstance()

        # Set all skills on cooldown
        inst.set_alarm(2, 30)
        inst.set_alarm(3, 180)
        inst.set_alarm(4, 240)
        inst.set_alarm(5, 300)
        self.assertEqual(inst.activeAlarmMask, 0b00111100)

        # Hourglass use item activates: alarm[2] = 0; alarm[3] = 0; alarm[4] = 0; alarm[5] = 0;
        for a in [2, 3, 4, 5]:
            inst.set_alarm(a, 0)

        # All alarms must be reset to -1 and mask cleared
        for a in [2, 3, 4, 5]:
            self.assertEqual(inst.get_alarm(a), -1, f"Alarm {a} must reset to -1 on Hourglass reset")
        self.assertEqual(inst.activeAlarmMask, 0, "activeAlarmMask must be 0 after reset")

if __name__ == '__main__':
    unittest.main()
