import pytest
from tools.gml_patcher import (
    patch_boss_shadow_effects,
    patch_guard_and_bullet,
    patch_heavy_boss_spawns,
    apply_all_gml_optimizations,
)

def test_boss_shadow_eliminates_instance_create_in_draw():
    # In oBoss2Clone_Draw_0, instance_create of oEfTrail must be replaced by direct ghost draw_sprite_ext
    gml = """
    if (random(1) < 0.3) {
        var gg = instance_create(xprevious, yprevious, oEfTrail);
        gg.sprite_index = sprite_index;
    }
    """
    patched = patch_boss_shadow_effects(gml)
    assert "instance_create" not in patched
    assert "draw_sprite_ext" in patched
    assert "c_black" in patched or "0.2" in patched

def test_guard_bullet_circle_decimation():
    gml = """
    draw_circle_color(x, y, 16, c_white, c_blue, false);
    draw_circle_color(x, y, 24, c_white, c_blue, false);
    """
    patched = patch_guard_and_bullet(gml)
    assert "__bullet_draw" in patched
    assert "draw_circle_color" in patched

def test_toxic_beast_boarlit_capping_and_activate_decimation():
    gml = """
    instance_activate_region(x - 200, y - 200, 400, 400, true);
    instance_create(x, y, oBoarM);
    """
    patched = patch_heavy_boss_spawns(gml)
    assert "instance_number(oBoarM) < 14" in patched
    assert "% 15" in patched

def test_wurm_lightning_and_circle_decimation():
    gml = """
    draw_circle(x, y, 32, false);
    draw_lightning(x, y, tx, ty, 5);
    """
    patched = patch_heavy_boss_spawns(gml)
    assert "view_xview" in patched
    assert "% 2" in patched
    assert "draw_lightning" in patched

def test_apply_all_gml_optimizations_coverage():
    scripts = {
        "gml_Object_oBoss2Clone_Draw_0": "var m = instance_create(x, y, oEfTrail);",
        "gml_Object_oGuardBullet_Draw_0": "draw_circle_color(x, y, 12, c_white, c_red, false);",
        "gml_Object_oBoar_Step_2": "instance_create(x, y, oBoarM); instance_activate_region(x, y, 100, 100, true);",
        "gml_Object_oWurmHead_Draw_0": "draw_circle(x, y, 20, false); draw_lightning(x, y, 10, 10, 2);",
    }
    patched_all = apply_all_gml_optimizations(scripts)
    assert "draw_sprite_ext" in patched_all["gml_Object_oBoss2Clone_Draw_0"]
    assert "__bullet_draw" in patched_all["gml_Object_oGuardBullet_Draw_0"]
    assert "instance_number(oBoarM) < 14" in patched_all["gml_Object_oBoar_Step_2"]
    assert "% 2" in patched_all["gml_Object_oWurmHead_Draw_0"]
