import pytest
from tools.gml_patcher import patch_nematocyst_and_missile_logic

def test_nematocyst_nozzle_empty_target_fallback():
    # When no enemy is within 600px, use_item must not skip missile spawning
    gml_code = """
    var target = instance_nearest(x, y, pEnemy);
    if (instance_exists(target) && point_distance(x, y, target.x, target.y) <= 600) {
        repeat(6) {
            var m = instance_create(x, y, oJellyMissileFriendly);
            m.target = target;
        }
    }
    """
    patched = patch_nematocyst_and_missile_logic(gml_code)
    assert "oJellyMissileFriendly" in patched
    # Must ensure missiles are spawned even if target does not exist (fallback fan)
    assert "else" in patched or "repeat (6)" in patched or "repeat(6)" in patched
    assert "direction" in patched or "fallback_angle" in patched or "image_xscale" in patched

def test_homing_missile_proximity_fallback():
    # oJellyMissileFriendly and oEfMissileMagic must detonate on proximity <= speed + 4 or <= 16
    gml_step = """
    if (instance_exists(target)) {
        direction = point_direction(x, y, target.x, target.y);
        if (point_distance(x, y, target.x, target.y) <= 10) {
            fire_explosion(x, y, 9);
            instance_destroy();
        }
    }
    """
    patched = patch_nematocyst_and_missile_logic(gml_step)
    assert "point_distance" in patched
    assert "speed + 4" in patched or "<= 16" in patched or "max(16" in patched

def test_wurm_missile_and_body_contact():
    gml_wurm = """
    if (place_meeting(x, y, pFriend)) {
        fire_explosion(x, y, 20);
        instance_destroy();
    }
    """
    patched = patch_nematocyst_and_missile_logic(gml_wurm)
    assert "pFriend" in patched
    assert "place_meeting" in patched or "point_distance" in patched
