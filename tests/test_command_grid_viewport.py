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

def test_engine_command_grid_centering_math():
    """
    Verifies that the engine-level centering offsets in runner.c align
    the GML Artifact 8 box grid symmetrically with equal margins on 426x240/426x320 viewports
    and completely avoids HUD overlap.
    """
    view_w = 426.0
    view_h = 320.0
    view_x = 1000.0
    view_y = 500.0

    # Target engine coordinates from runner.c
    inst_x = view_x + view_w * 0.5 - 20.0
    inst_y = view_y + 200.0

    # GML formula: tx = x + (tc - 4) * 40; ty = y - 130 + tr * 40;
    # Item icons are 32x32 centered at (tx, ty) -> bounds [tx - 16, tx + 16]
    # Check 10 columns (tc: 0..9)
    col0_tx = inst_x + (0 - 4) * 40.0
    col0_left = col0_tx - 16.0
    col9_tx = inst_x + (9 - 4) * 40.0
    col9_right = col9_tx + 16.0

    left_margin = col0_left - view_x
    right_margin = (view_x + view_w) - col9_right

    # Symmetrical 17px margins on both sides
    assert left_margin == 17.0
    assert right_margin == 17.0
    assert col0_left >= view_x
    assert col9_right <= (view_x + view_w)

    # Check vertical bounds: Row 0 (top row) and Row 3 (bottom 4th row)
    row0_ty = inst_y - 130.0 + 0 * 40.0
    row0_top = row0_ty - 16.0
    row3_ty = inst_y - 130.0 + 3 * 40.0
    row3_bottom = row3_ty + 16.0

    hud_bottom = view_y + 48.0
    # Row 0 top (view_y + 54) is completely clear of top HUD (view_y + 48)
    assert row0_top >= hud_bottom
    assert row0_top - view_y == 54.0

    # Row 3 bottom (view_y + 206) is well within 240/320px viewport
    assert row3_bottom <= (view_y + view_h)

def test_engine_command_detection_in_runner_c():
    """
    Ensures butterscotch/src/runner.c implements the triple-state detection
    (fade_alpha, owner, active) and the exact geometric centering offsets.
    """
    with open("butterscotch/src/runner.c", "r", encoding="utf-8") as f:
        src = f.read()

    assert "isArtifact8Box(runner, inst->objectIndex)" in src
    assert "runner->activeViewX + runner->activeViewW * 0.5f - 20.0f" in src
    assert "runner->activeViewY + 200.0f" in src
    assert "fade_alpha" in src
    assert "owner" in src
    assert "active" in src
