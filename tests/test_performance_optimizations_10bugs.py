import pytest
from tools.gml_patcher import (
    patch_damage_text_batching,
    patch_clay_man_ai,
    patch_boss_shadow_effects,
    apply_all_gml_optimizations
)

def test_damage_text_instance_capping():
    code = "instance_create(x, y, oDamageText);"
    patched = patch_damage_text_batching(code)
    assert "accumulated_damage" in patched or "instance_number" in patched

def test_clay_man_raycast_staggering():
    step_code = "if (collision_line(x, y, target.x, target.y, pBlock, false, true) == noone) { move(); }"
    patched = patch_clay_man_ai(step_code)
    # Must decimate raycast frequency using instance ID and time
    assert "%" in patched or "alarm" in patched
    assert "collision_line" in patched

def test_boss_shadow_particle_throttling():
    boss_code = "part_particles_create(sys, x, y, pt_Shadow, 1);"
    patched = patch_boss_shadow_effects(boss_code)
    assert "part_particles_create" in patched
    assert "%" in patched or "time" in patched.lower()

def test_apply_all_gml_optimizations_includes_clay_and_boss():
    scripts = {
        "gml_Script_step_clay": "if (collision_line(x, y, target.x, target.y, pBlock, false, true) == noone) { move(); }",
        "gml_Object_oBoss2Clone_Step_0": "part_particles_create(sys, x, y, pt_Shadow, 1);"
    }
    patched = apply_all_gml_optimizations(scripts)
    assert "%" in patched["gml_Script_step_clay"]
    assert "%" in patched["gml_Object_oBoss2Clone_Step_0"]
