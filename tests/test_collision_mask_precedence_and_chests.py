import os
import unittest


class TestCollisionMaskPrecedenceAndChests(unittest.TestCase):
    def setUp(self):
        self.repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.butterscotch_src = os.path.join(self.repo_root, "butterscotch", "src")

    def test_collision_get_sprite_precedence_in_collision_h(self):
        """Collision_getSprite must prioritize maskIndex, then obj->textureMaskId, then spriteIndex, then obj->spriteId."""
        path = os.path.join(self.butterscotch_src, "collision.h")
        self.assertTrue(os.path.exists(path), f"Missing {path}")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static inline Sprite* Collision_getSprite(")
        self.assertNotEqual(func_start, -1, "Missing Collision_getSprite in collision.h")
        func_end = src.find("static inline InstanceBBox Collision_computeBBox(", func_start)
        func_body = src[func_start:func_end]

        pos_mask_idx = func_body.find("inst->maskIndex")
        pos_tex_mask = func_body.find("obj->textureMaskId")
        pos_spr_idx = func_body.find("inst->spriteIndex")
        pos_spr_id = func_body.find("obj->spriteId")

        self.assertNotEqual(pos_mask_idx, -1, "Missing inst->maskIndex check in Collision_getSprite")
        self.assertNotEqual(pos_tex_mask, -1, "Missing obj->textureMaskId check in Collision_getSprite")
        self.assertNotEqual(pos_spr_idx, -1, "Missing inst->spriteIndex check in Collision_getSprite")
        self.assertNotEqual(pos_spr_id, -1, "Missing obj->spriteId check in Collision_getSprite")

        # Verify strict GM:S 1.4 precedence order:
        # 1. inst->maskIndex
        # 2. obj->textureMaskId (object collision mask assigned in GameMaker IDE)
        # 3. inst->spriteIndex (current instance appearance sprite)
        # 4. obj->spriteId (object fallback appearance sprite)
        self.assertTrue(pos_mask_idx < pos_tex_mask, "inst->maskIndex must precede obj->textureMaskId")
        self.assertTrue(pos_tex_mask < pos_spr_idx, "obj->textureMaskId must precede inst->spriteIndex")
        self.assertTrue(pos_spr_idx < pos_spr_id, "inst->spriteIndex must precede obj->spriteId")

    def test_sprite_set_bbox_mode_uses_collision_get_sprite(self):
        """builtin_sprite_set_bbox_mode must use Collision_getSprite to properly dirty instances using object masks."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static RValue builtin_sprite_set_bbox_mode(")
        self.assertNotEqual(func_start, -1, "Missing builtin_sprite_set_bbox_mode")
        func_end = src.find("static RValue builtin_sprite_set_bbox(", func_start)
        func_body = src[func_start:func_end]

        self.assertIn("Collision_getSprite(runner->dataWin, inst) == spr", func_body,
                      "builtin_sprite_set_bbox_mode must check Collision_getSprite against spr")
        self.assertNotIn("int32_t activeMask = (inst->maskIndex >= 0)", func_body,
                         "builtin_sprite_set_bbox_mode must not use legacy decoupled maskIndex ternary")

    def test_move_contact_common_no_artificial_upward_teleportation(self):
        """moveContactCommon must never artificially elevate instances (chests, barrels, doors, players) upwards."""
        path = os.path.join(self.butterscotch_src, "vm_builtins.c")
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            src = f.read()

        func_start = src.find("static void moveContactCommon(")
        self.assertNotEqual(func_start, -1, "Missing moveContactCommon")
        func_end = src.find("static RValue builtin_move_contact_solid(", func_start)
        func_body = src[func_start:func_end]

        self.assertNotIn("inst->y -= (float) up;", func_body,
                         "moveContactCommon must not modify inst->y upwards")
        self.assertNotIn("for (int32_t up = 16;", func_body,
                         "moveContactCommon must not loop upwards")
        self.assertIn("if (!bounceTestFree(runner, inst, inst->x, inst->y, useall))", func_body)
        self.assertIn("return;", func_body)


if __name__ == "__main__":
    unittest.main()
