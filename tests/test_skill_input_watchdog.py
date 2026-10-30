import pytest
from tools.gml_patcher import patch_player_skill_watchdog, apply_all_gml_optimizations

def test_player_skill_activity_watchdog_injection():
    step_player_code = """
    // Player step logic
    if (activity == 2) {
        // skill 2 execution
    }
    """
    patched = patch_player_skill_watchdog(step_player_code)
    assert "activity_watchdog" in patched or "activity_timer" in patched or "skill2" in patched
    assert "activity = 0" in patched

def test_player_cooldown_underflow_protection():
    code = """
    if (skill2_cooldown > 0) skill2_cooldown -= 1;
    """
    patched = patch_player_skill_watchdog(code)
    assert "skill2_cooldown = 0" in patched or "<= 0" in patched

def test_apply_all_gml_optimizations_applies_to_step_player():
    scripts = {
        "gml_Script_step_player": "if (activity == 2) { execute_skill2(); }"
    }
    patched = apply_all_gml_optimizations(scripts)
    assert "activity = 0" in patched["gml_Script_step_player"]
