import pytest
from tools.gml_patcher import patch_command_artifact_grid_bounds, apply_all_gml_optimizations

def test_command_artifact_grid_is_clamped_to_view():
    gml_code = """
    var draw_x = x - total_width / 2;
    var draw_y = y - total_height - 20;
    """
    patched = patch_command_artifact_grid_bounds(gml_code)
    assert "view_xview[0]" in patched
    assert "view_yview[0]" in patched
    assert "clamp(" in patched or "min(" in patched

def test_command_artifact_hud_safe_zone():
    # Grid should not spawn within top 48px where HP bar & skills live under handheld scaling
    gml_code = """
    draw_y = y - 100;
    """
    patched = patch_command_artifact_grid_bounds(gml_code)
    assert "48" in patched or "hud" in patched.lower()
    assert "view_yview[0]" in patched

def test_apply_all_gml_optimizations_applies_to_artifact_and_select():
    scripts = {
        "gml_Object_pArtifact8Box_Step_0": "var draw_x = x - total_width / 2; var draw_y = y - 100;",
        "gml_Object_oSelect_Draw_0": "draw_sprite(sSelect, 0, x, y);"
    }
    patched_scripts = apply_all_gml_optimizations(scripts)
    assert "view_xview[0]" in patched_scripts["gml_Object_pArtifact8Box_Step_0"]
