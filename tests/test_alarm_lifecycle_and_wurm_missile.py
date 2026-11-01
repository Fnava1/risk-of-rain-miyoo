import unittest
import os
import re

class TestAlarmLifecycleAndWurmMissile(unittest.TestCase):
    """
    Validates the fix for the Gilded Wurm missile zero-damage bug and the alarm lifecycle.

    In Risk of Rain:
    - Phase 2 Gilded Blue Wurm shoots homing missiles (`oWurmMissile`).
    - On collision with player in Step_End, `oWurmMissile` calls:
        bullet = fire_explosion(3, x, y, 1, 292, 1, 522, 0.8, 1);
        bullet.percent_hp = 0.05;
        instance_destroy();
    - `fire_explosion` sets `bullet.alarm[0] = 1`.
    - In GameMaker, on Frame 1's Alarm phase, `alarm[0]` decrements from 1 to 0.
      Alarm 0 triggers, and the alarm remains 0 throughout the Step and Collision phases of Frame 1.
    - In `gml_Object_oExplosion_Collision_325` (collision with player), bytecode checks:
        if (alarm[0] != -1) { deal damage }
    - In Frame 2's Alarm phase, the alarm transitions from 0 to -1 (inactive).

    Prior bug in Butterscotch:
    - Decrementing 1 -> 0 immediately set `inst->alarm[0] = -1` before Collision executed,
      and `vm_builtins.c` getter returned -1.0 for `a <= 0`.
    - Consequently, `alarm[0] != -1` evaluated to FALSE on the active collision frame,
      completely silencing all missile damage (0 damage dealt).

    Fix:
    - `vm_builtins.c` getter returns -1.0 only for `a < 0`, returning 0.0 for running trigger frame.
    - `runner.c` decrements `alarm > 0`, fires at 0, and transitions to -1 on the subsequent step.
    """

    def setUp(self):
        self.root_dir = os.path.dirname(os.path.dirname(__file__))
        self.vm_builtins_path = os.path.join(self.root_dir, 'butterscotch', 'src', 'vm_builtins.c')
        self.runner_path = os.path.join(self.root_dir, 'butterscotch', 'src', 'runner.c')

    def test_source_code_alarm_getter(self):
        """Verify vm_builtins.c getter preserves 0 for active alarms and returns -1.0 only for a < 0."""
        self.assertTrue(os.path.exists(self.vm_builtins_path), "vm_builtins.c not found")
        with open(self.vm_builtins_path, 'r', encoding='utf-8', errors='ignore') as f:
            src = f.read()

        # Check BUILTIN_VAR_ALARM
        var_match = re.search(
            r'case BUILTIN_VAR_ALARM:\s*\{[\s\S]*?int32_t a = inst->alarm\[arrayIndex\];\s*if\s*\(a\s*<\s*0\)\s*return RValue_makeReal\(-1\.0\);[\s\S]*?return RValue_makeReal\(\(GMLReal\)\s*a\);',
            src
        )
        self.assertIsNotNone(var_match, "BUILTIN_VAR_ALARM must check a < 0 (returning 0.0 when alarm is 0)")

        # Check builtin_alarm_get
        fn_match = re.search(
            r'builtin_alarm_get\([\s\S]*?int32_t a = inst->alarm\[alarmIndex\];\s*if\s*\(a\s*<\s*0\)\s*return RValue_makeReal\(-1\.0\);[\s\S]*?return RValue_makeReal\(\(GMLReal\)\s*a\);',
            src
        )
        self.assertIsNotNone(fn_match, "builtin_alarm_get must check a < 0 (returning 0.0 when alarm is 0)")

    def test_source_code_runner_alarm_lifecycle(self):
        """Verify runner.c alarm loop decrements when > 0, fires at 0, and transitions to -1 on next step."""
        self.assertTrue(os.path.exists(self.runner_path), "runner.c not found")
        with open(self.runner_path, 'r', encoding='utf-8', errors='ignore') as f:
            src = f.read()

        runner_match = re.search(
            r'if\s*\(inst->alarm\[alarmIdx\]\s*>\s*0\)\s*\{[\s\S]*?inst->alarm\[alarmIdx\]--;[\s\S]*?if\s*\(inst->alarm\[alarmIdx\]\s*==\s*0\)\s*\{[\s\S]*?Runner_executeResolvedEvent[\s\S]*?else\s*\{[\s\S]*?inst->alarm\[alarmIdx\]\s*=\s*-1;\s*inst->activeAlarmMask\s*&=',
            src
        )
        self.assertIsNotNone(runner_match, "runner.c must maintain GameMaker alarm trigger lifecycle")

    def test_wurm_missile_and_explosion_simulation(self):
        """Simulate oWurmMissile impacting player and oExplosion dealing percent_hp damage."""
        class MockAlarmEngine:
            def __init__(self):
                self.alarm = [-1] * 12
                self.activeAlarmMask = 0
                self.event_fired = False

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
                if val < 0:
                    return -1
                return val

            def tick_alarms(self):
                self.event_fired = False
                for alarmIdx in range(12):
                    bit = 1 << alarmIdx
                    if (self.activeAlarmMask & bit) == 0:
                        continue
                    if self.alarm[alarmIdx] > 0:
                        self.alarm[alarmIdx] -= 1
                        if self.alarm[alarmIdx] == 0:
                            self.event_fired = True
                    else:
                        self.alarm[alarmIdx] = -1
                        self.activeAlarmMask &= ~bit

        # Spawn oExplosion as done by oWurmMissile
        explosion = MockAlarmEngine()
        explosion.set_alarm(0, 1) # bullet.alarm[0] = 1
        percent_hp = 0.05
        player_maxhp = 500
        player_hp = 500

        # Frame 0: Spawned
        self.assertEqual(explosion.alarm[0], 1)
        self.assertEqual(explosion.activeAlarmMask, 1)

        # Frame 1: Alarm phase
        explosion.tick_alarms()
        self.assertTrue(explosion.event_fired, "Alarm 0 event must fire on Frame 1")
        self.assertEqual(explosion.alarm[0], 0, "Alarm 0 must be in trigger state (0) during Frame 1")
        self.assertEqual(explosion.get_alarm(0), 0, "Getter must return 0.0 on trigger frame")

        # Frame 1: Collision phase (gml_Object_oExplosion_Collision_325)
        # Bytecode check: if (alarm[0] != -1)
        collision_gate = (explosion.get_alarm(0) != -1)
        self.assertTrue(collision_gate, "Collision gate (alarm[0] != -1) MUST pass on trigger frame")

        # Damage calculation
        damage = player_maxhp * percent_hp # 25 damage
        player_hp -= damage
        self.assertEqual(player_hp, 475, "Player must take 5% max HP damage")

        # Frame 2: Alarm phase
        explosion.tick_alarms()
        self.assertFalse(explosion.event_fired, "Alarm 0 must not fire again on Frame 2")
        self.assertEqual(explosion.alarm[0], -1, "Alarm 0 must transition to -1 on Frame 2")
        self.assertEqual(explosion.get_alarm(0), -1, "Getter must return -1.0 on Frame 2")
        self.assertEqual(explosion.activeAlarmMask, 0, "Alarm mask must be cleared on Frame 2")

        # Frame 2: Collision phase
        collision_gate_frame2 = (explosion.get_alarm(0) != -1)
        self.assertFalse(collision_gate_frame2, "Collision gate must fail on Frame 2 (no double damage)")
        self.assertEqual(player_hp, 475, "Player health must remain unchanged on Frame 2")

if __name__ == '__main__':
    unittest.main()
