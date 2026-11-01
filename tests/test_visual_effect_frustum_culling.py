#!/usr/bin/env python3
"""
Unit test for extended viewport frustum culling of visual effect entities.
Verifies that:
1. Pure visual effect objects (oExplosion, oEfDamage, oEfGold, oEfFirework, oEfTrail, etc.)
   are culled when outside the active viewport (activeViewX, activeViewY, activeViewW, activeViewH)
   plus the safety margin (96px * maxScale).
2. Pure visual effect objects INSIDE the viewport or within safety margin are drawn.
3. Non-effect entities (players, enemies, bosses) are not affected by this draw-cull.
4. Non-gameplay rooms or GUI passes disable frustum culling.
"""

import unittest

class SimulatedRunnerView:
    def __init__(self, vx=0, vy=0, vw=640, vh=480):
        self.activeViewX = float(vx)
        self.activeViewY = float(vy)
        self.activeViewW = float(vw)
        self.activeViewH = float(vh)
        self.activeViewValid = True
        self.inGuiPass = False
        self.currentRoom = "rLevel1"
        self.cullable_objects = {
            "oExplosion", "oEfDamage", "oEfGold", "oEfExp", "oEfNugget",
            "oEfFirework", "oEfFireworkDust", "oEfTrail", "oEfChain",
            "oEfLaser", "oEfLaserLine", "oEfRay", "oEfRayFront",
            "oEfFlash", "oEfCircle", "oEfDodge", "oEfOutline",
            "oEfSparks", "oEfDust1", "oEfDust2", "oMushDust",
            "oBlood", "oBloodDecal"
        }

    def is_non_gameplay_room(self, room_name):
        return room_name in ("rMenu", "rSelect", "rCutscene", "rGameOver")

    def should_cull_draw(self, obj_name, x, y, scale_x=1.0, scale_y=1.0):
        if obj_name not in self.cullable_objects:
            return False

        if not self.activeViewValid or self.inGuiPass or self.is_non_gameplay_room(self.currentRoom):
            return False

        margin = 96.0
        max_scale = max(abs(scale_x), abs(scale_y))
        if max_scale > 1.0:
            margin *= max_scale

        if (x < self.activeViewX - margin or
            x > self.activeViewX + self.activeViewW + margin or
            y < self.activeViewY - margin or
            y > self.activeViewY + self.activeViewH + margin):
            return True # Cull!

        return False


class TestVisualEffectFrustumCulling(unittest.TestCase):
    def setUp(self):
        self.runner = SimulatedRunnerView(0, 0, 640, 480)

    def test_effect_inside_viewport(self):
        # Center of screen (320, 240)
        self.assertFalse(self.runner.should_cull_draw("oEfGold", 320, 240))
        self.assertFalse(self.runner.should_cull_draw("oEfFirework", 10, 10))

    def test_effect_within_safety_margin(self):
        # 50px to the left of viewport (X = -50, margin is 96): should NOT cull yet
        self.assertFalse(self.runner.should_cull_draw("oEfTrail", -50, 240))
        # 50px below viewport (Y = 530, margin is 96): should NOT cull yet
        self.assertFalse(self.runner.should_cull_draw("oEfDamage", 320, 530))

    def test_effect_outside_viewport(self):
        # 200px to the right of viewport (X = 840, margin is 96): SHOULD cull
        self.assertTrue(self.runner.should_cull_draw("oEfGold", 840, 240))
        # 200px above viewport (Y = -200): SHOULD cull
        self.assertTrue(self.runner.should_cull_draw("oExplosion", 320, -200))
        # Far off-screen
        self.assertTrue(self.runner.should_cull_draw("oEfFirework", 2000, 3000))

    def test_non_effect_object_never_culled(self):
        # Player or Boss off-screen should not be culled by this effect pass
        self.assertFalse(self.runner.should_cull_draw("oP", 1500, 1500))
        self.assertFalse(self.runner.should_cull_draw("oLizard", 2000, 2000))
        self.assertFalse(self.runner.should_cull_draw("oProvidence", 3000, 3000))

    def test_menu_or_gui_pass_disables_culling(self):
        self.runner.inGuiPass = True
        self.assertFalse(self.runner.should_cull_draw("oEfGold", 2000, 2000))
        self.runner.inGuiPass = False

        self.runner.currentRoom = "rMenu"
        self.assertFalse(self.runner.should_cull_draw("oEfGold", 2000, 2000))


if __name__ == '__main__':
    unittest.main()
