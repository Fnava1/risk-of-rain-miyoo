import pytest
from tools.gml_patcher import patch_stage_transition_pacing, apply_all_gml_optimizations

def test_storage_menu_ini_loop_optimization():
    # Loop opening and closing INI per iteration should be optimized
    gml_code = """
    for (i = 0; i < 120; i++) {
        ini_open("Save.ini");
        val = ini_read_real("Items", string(i), 0);
        ini_close();
    }
    """
    patched = patch_stage_transition_pacing(gml_code)
    # Inside the loop, repeated ini_open should be removed/lifted or consolidated
    assert patched.count('ini_open') <= 1

def test_room_transition_loading_indication():
    # Audio should not hard cut abruptly right before room transition
    gml_code = """
    audio_stop_all();
    room_goto_next();
    """
    patched = patch_stage_transition_pacing(gml_code)
    assert "room_goto_next" in patched
    assert "audio_sound_gain" in patched or "draw_clear" in patched or "audio_stop_all" not in patched

def test_final_stage_ship_transition():
    # Penultimate stage transition to final level (rShip)
    gml_code = """
    if (stage == 5) {
        audio_stop_all();
        room_goto(rShip);
    }
    """
    patched = patch_stage_transition_pacing(gml_code)
    assert "rShip" in patched
    assert "audio_stop_all" not in patched or "draw" in patched or "audio_sound_gain" in patched
