import pytest
from tools.gml_patcher import (
    relocate_hud_coordinates,
    inject_distance_decimation_guard,
    patch_damage_text_batching
)

def test_distance_decimation_does_not_use_undefined_frame_counter():
    """Ensure distance decimation uses built-in current_time, not non-existent global.frame_counter."""
    sample = "mp_potential_step(oPlayer.x, oPlayer.y, 2, false);"
    patched = inject_distance_decimation_guard(sample)
    
    assert "global.frame_counter" not in patched
    assert "current_time" in patched

def test_distance_decimation_preserves_physics_and_gravity():
    """Ensure the guard wraps pathfinding/AI rather than aborting the entire Step event with exit;"""
    sample = """
    vspeed += gravity;
    if (place_meeting(x, y + vspeed, oSolid)) vspeed = 0;
    mp_potential_step(oPlayer.x, oPlayer.y, 2, false);
    """
    patched = inject_distance_decimation_guard(sample)
    
    # vspeed and gravity must still execute even if distant
    assert "vspeed += gravity" in patched
    # Only AI should be conditional
    assert "exit;" not in patched

def test_damage_text_batching_accumulates_real_damage():
    """Ensure damage text batching aggregates the actual damage amount, not a hardcoded +1."""
    sample = "instance_create(x, y, oDamageText);"
    patched = patch_damage_text_batching(sample)
    
    assert "accumulated_damage += 1;" not in patched
    assert "accumulated_damage +=" in patched
    assert "argument0" in patched or "damage" in patched

def test_relocate_hud_does_not_corrupt_non_y_parameters():
    """Ensure regex does not blindly rewrite colors (like 300) or coordinates that are not Y-offsets."""
    sample = "draw_sprite_ext(spr_icon, 0, 320, 340, 1, 1, 0, c_white, 1);"
    patched = relocate_hud_coordinates(sample)
    
    # 320 is X-coordinate and must remain 320!
    # 340 is Y-coordinate and should shift to 440.
    assert "320" in patched
    assert "440" in patched
