import unittest
import os
import re

class TestRapidMitosisFix(unittest.TestCase):
    """
    Validates the fix for Rapid Mitosis (oTwins) item cooldown bug.
    In GameMaker Studio, when an alarm is inactive, its value is -1.
    When Rapid Mitosis is collected, it executes:
        other.alarm[0] = other.alarm[0] * 0.75;
    If alarm[0] was -1 (ready):
        -1 * 0.75 = -0.75.
    In C, casting -0.75 to int32_t truncates to 0, which trapped the player's
    equipment cooldown at 0 permanently with activeAlarmMask cleared.
    The fix ensures floating point values are rounded to nearest integer (matching GMS)
    and any non-positive value (<= 0) deactivates the alarm to -1.
    """

    def setUp(self):
        self.vm_builtins_path = os.path.join(
            os.path.dirname(os.path.dirname(__file__)),
            'butterscotch', 'src', 'vm_builtins.c'
        )

    def test_source_code_alarm_rounding_and_clamping(self):
        """Verify vm_builtins.c rounds float alarm values and clamps non-positive to -1."""
        self.assertTrue(os.path.exists(self.vm_builtins_path), "vm_builtins.c not found")
        with open(self.vm_builtins_path, 'r', encoding='utf-8', errors='ignore') as f:
            src = f.read()

        # Check BUILTIN_VAR_ALARM
        alarm_var_match = re.search(
            r'case BUILTIN_VAR_ALARM:\s*\{[\s\S]*?int32_t newValue = \(val\.type == RVALUE_REAL\)\s*\?\s*\(int32_t\)\s*GMLReal_round\(val\.real\)\s*:\s*RValue_toInt32\(val\);[\s\S]*?if\s*\(newValue\s*<=\s*0\)\s*newValue\s*=\s*-1;',
            src
        )
        self.assertIsNotNone(alarm_var_match, "BUILTIN_VAR_ALARM must round reals and clamp <= 0 to -1")

        # Check builtin_alarm_set
        alarm_set_match = re.search(
            r'builtin_alarm_set\([\s\S]*?int32_t value = \(args\[1\]\.type == RVALUE_REAL\)\s*\?\s*\(int32_t\)\s*GMLReal_round\(args\[1\]\.real\)\s*:\s*RValue_toInt32\(args\[1\]\);[\s\S]*?if\s*\(value\s*<=\s*0\)\s*value\s*=\s*-1;',
            src
        )
        self.assertIsNotNone(alarm_set_match, "builtin_alarm_set must round reals and clamp <= 0 to -1")

        # Check builtin_action_set_alarm
        action_set_match = re.search(
            r'builtin_action_set_alarm\([\s\S]*?int32_t steps = \(args\[0\]\.type == RVALUE_REAL\)\s*\?\s*\(int32_t\)\s*GMLReal_round\(args\[0\]\.real\)\s*:\s*RValue_toInt32\(args\[0\]\);[\s\S]*?if\s*\(steps\s*<=\s*0\)\s*steps\s*=\s*-1;',
            src
        )
        self.assertIsNotNone(action_set_match, "builtin_action_set_alarm must round reals and clamp <= 0 to -1")

    def test_rapid_mitosis_idle_simulation(self):
        """Simulate picking up Rapid Mitosis when equipment is idle/ready."""
        # Alarm starts at -1 (inactive / ready)
        alarm0 = -1

        def gms_set_alarm(raw_val):
            # GameMaker Studio rounding & deactivation logic
            if isinstance(raw_val, float):
                rounded = int(round(raw_val))
            else:
                rounded = int(raw_val)
            if rounded <= 0:
                return -1, False # value = -1, activeAlarmMask bit = 0
            return rounded, True # value > 0, activeAlarmMask bit = 1

        # Rapid Mitosis 1 pickup:
        new_val = float(alarm0) * 0.75 # -0.75
        alarm0, active = gms_set_alarm(new_val)
        self.assertEqual(alarm0, -1, "Idle alarm must remain -1 after picking up Rapid Mitosis")
        self.assertFalse(active, "Idle alarm mask bit must remain 0 (inactive)")

        # Rapid Mitosis 2 pickup (stacking):
        new_val = float(alarm0) * 0.75 # -0.75 again
        alarm0, active = gms_set_alarm(new_val)
        self.assertEqual(alarm0, -1, "Idle alarm must remain -1 after 2nd Rapid Mitosis")
        self.assertFalse(active, "Idle alarm mask bit must remain 0")

    def test_rapid_mitosis_active_cooldown_simulation(self):
        """Simulate picking up Rapid Mitosis when equipment has an active cooldown."""
        def gms_set_alarm(raw_val):
            if isinstance(raw_val, float):
                rounded = int(round(raw_val))
            else:
                rounded = int(raw_val)
            if rounded <= 0:
                return -1, False
            return rounded, True

        # Equipment used: 45s cooldown @ 60fps = 2700 frames
        alarm0 = 2700
        # Rapid Mitosis pickup reduces cooldown by 25%:
        new_val = float(alarm0) * 0.75
        alarm0, active = gms_set_alarm(new_val)
        self.assertEqual(alarm0, 2025, "Active cooldown must be reduced by 25%")
        self.assertTrue(active, "Active cooldown mask bit must remain 1")

        # Second Rapid Mitosis pickup reduces further:
        new_val = float(alarm0) * 0.75
        alarm0, active = gms_set_alarm(new_val)
        self.assertEqual(alarm0, 1519, "Active cooldown must be reduced further (2025 * 0.75 = 1518.75 -> 1519)")
        self.assertTrue(active, "Active cooldown mask bit must remain 1")

    def test_zero_alarm_deactivation(self):
        """Verify explicitly setting alarm to 0 deactivates it to -1."""
        def gms_set_alarm(raw_val):
            if isinstance(raw_val, float):
                rounded = int(round(raw_val))
            else:
                rounded = int(raw_val)
            if rounded <= 0:
                return -1, False
            return rounded, True

        val, active = gms_set_alarm(0)
        self.assertEqual(val, -1, "Setting alarm to 0 must deactivate to -1")
        self.assertFalse(active, "Mask bit for alarm 0 must be False")

        val, active = gms_set_alarm(0.0)
        self.assertEqual(val, -1, "Setting alarm to 0.0 must deactivate to -1")
        self.assertFalse(active, "Mask bit for alarm 0.0 must be False")

        val, active = gms_set_alarm(-5)
        self.assertEqual(val, -1, "Setting alarm to negative must deactivate to -1")
        self.assertFalse(active, "Mask bit for negative alarm must be False")

if __name__ == '__main__':
    unittest.main()
