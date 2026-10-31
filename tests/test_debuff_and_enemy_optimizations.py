import pytest
from tools.gml_patcher import (
    patch_command_artifact_grid_bounds,
    patch_blaze_particles,
    patch_poison_entity_scanning,
    patch_chain_lightning_fx,
    patch_enemy_aura_and_fog,
    patch_heavy_boss_spawns,
    apply_all_gml_optimizations
)

def test_command_grid_centers_on_viewport():
    """Verify Command artifact item selection grid centers relative to viewport camera, not chest world (x,y)."""
    sample = """
    tx = x + (tc - 4) * 40;
    ty = y - 130 + tr * 40;
    """
    patched = patch_command_artifact_grid_bounds(sample)

    assert "view_xview[0]" in patched
    assert "view_wview[0]" in patched
    assert "view_yview[0]" in patched
    assert "tc * 38" in patched
    assert "tr * 38" in patched
    assert "+ 52" in patched

def test_blaze_particles_decimation():
    """Verify burning/blaze particle emissions are decimated to 1 out of 3 frames."""
    sample = "part_particles_create_color(pt_fire, x, y, c_orange, 1);"
    patched = patch_blaze_particles(sample)

    assert "% 3" in patched
    assert "current_time" in patched
    assert "part_particles_create_color" in patched

def test_poison_scanning_decimation():
    """Verify instance_nearest_variable in poison debuff is staggered across instances."""
    sample = "if (instance_nearest_variable(x, y, oEnemy, \"poison\") != noone) { target = id; }"
    patched = patch_poison_entity_scanning(sample)

    assert "% 4" in patched
    assert "__mmp_poison_scan" in patched
    assert "instance_nearest_variable" in patched

def test_chain_lightning_circle_throttling():
    """Verify heavy filled circle draws in chain lightning (Ukulele/Tesla) are throttled."""
    sample = "draw_circle(x, y, 16, false);"
    patched = patch_chain_lightning_fx(sample)

    assert "% 2" in patched
    assert "draw_circle" in patched

def test_enemy_aura_culling_and_fog_throttle():
    """Verify luminous glow auras (bm_add) are viewport-culled and damage fog flashes are throttled."""
    sample = """
    draw_set_blend_mode(bm_add);
    draw_sprite_ext(sChildGlow, 0, x, y, 1.5, 1.5, 0, c_yellow, 0.6);
    draw_set_blend_mode(bm_normal);
    d3d_set_fog(true, c_white, 0, 0);
    draw_self();
    d3d_set_fog(false, c_black, 0, 0);
    """
    patched = patch_enemy_aura_and_fog(sample)

    assert "view_xview[0]" in patched
    assert "view_wview[0]" in patched
    assert "draw_set_blend_mode(bm_add)" in patched
    assert "__mmp_fog_tick % 2" in patched

def test_heavy_boss_spawns_and_activation():
    """Verify Toxic Beast instance_activate_region is decimated to every 15 frames."""
    sample = """
    instance_activate_region(x - 400, y - 400, 800, 800, true);
    draw_circle(x, y, 32, false);
    """
    patched = patch_heavy_boss_spawns(sample)

    assert "% 15" in patched
    assert "instance_activate_region" in patched
    assert "view_xview" in patched

def test_apply_all_gml_optimizations_with_new_rules():
    """Verify batch patching routes new debuffs and enemies properly."""
    scripts = {
        "pArtifact8Box_Draw": "tx = x + tc * 40; ty = y + tr * 40;",
        "oEfBlaze_Step": "part_particles_create_color(pt, x, y, c_red, 1);",
        "oEfPoison_Step": "if (instance_nearest_variable(x, y, oEnemy, \"p\") != noone) exit;",
        "oChainLightning_Draw": "draw_circle(x, y, 8, false);",
        "draw_ai_default": "draw_set_blend_mode(bm_add); draw_self(); draw_set_blend_mode(bm_normal);",
        "oBoar_Step": "instance_activate_region(x, y, 100, 100, true);"
    }
    out = apply_all_gml_optimizations(scripts)

    assert "view_wview[0]" in out["pArtifact8Box_Draw"]
    assert "% 3" in out["oEfBlaze_Step"]
    assert "% 4" in out["oEfPoison_Step"]
    assert "% 2" in out["oChainLightning_Draw"]
    assert "view_xview[0]" in out["draw_ai_default"]
    assert "% 15" in out["oBoar_Step"]
