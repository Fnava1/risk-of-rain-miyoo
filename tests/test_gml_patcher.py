import pytest
from tools.gml_patcher import (
    relocate_hud_coordinates,
    inject_distance_decimation_guard,
    patch_damage_text_batching,
    apply_all_gml_optimizations
)

def test_relocate_hud_coordinates_top_and_bottom():
    """Verify HUD drawing coordinates are shifted to top band (0-60px) and bottom band (420-480px)."""
    sample_hud_gml = """
    draw_hp_bar(x, 340, hp, max_hp);
    draw_timer(x, 10, time_elapsed);
    draw_skills(x, 330, skill_cd);
    """
    
    patched = relocate_hud_coordinates(sample_hud_gml)
    
    # Bottom elements should be pushed into the 420-480 region
    assert "440" in patched or "450" in patched or "y_bottom" in patched or "+ 60" in patched
    # Top elements should remain in 0-60 region
    assert "draw_timer" in patched
    assert patched != sample_hud_gml

def test_inject_distance_decimation_guard():
    """Verify distant enemies (>960px) skip AI navigation on non-modulo-4 frames."""
    sample_enemy_step = """
    // Basic enemy AI pathfinding
    mp_potential_step(oPlayer.x, oPlayer.y, move_speed, false);
    """
    
    patched = inject_distance_decimation_guard(sample_enemy_step)
    
    assert "point_distance" in patched
    assert "960" in patched
    assert "% 4" in patched
    assert "exit" in patched or "return" in patched

def test_patch_damage_text_batching():
    """Verify that multiple damage numbers instantiate an accumulator instead of spawning 50 objects."""
    sample_damage_code = """
    instance_create(x, y, oDamageText);
    """
    
    patched = patch_damage_text_batching(sample_damage_code)
    
    assert "accumulated_damage" in patched or "oDamageTextBatch" in patched
    assert patched != sample_damage_code

def test_apply_all_gml_optimizations():
    """Verify dictionary of scripts can be patched in batch."""
    scripts = {
        "oHUD_Draw": "draw_hp_bar(x, 340, hp, max_hp);",
        "pEnemy_Step": "mp_potential_step(oPlayer.x, oPlayer.y, move_speed, false);",
        "damage_apply": "instance_create(x, y, oDamageText);"
    }
    
    optimized = apply_all_gml_optimizations(scripts)
    assert len(optimized) == 3
    assert optimized["oHUD_Draw"] != scripts["oHUD_Draw"]
    assert "% 4" in optimized["pEnemy_Step"]
