"""
GML Bytecode Patcher for Risk of Rain (2013) on Miyoo Mini Plus.
Injects:
1. HUD coordinate relocation (shifts elements to 0-60 top and 420-480 bottom bands).
2. Distance-based tick decimation for distant enemies (>960px).
3. Damage text batching to avoid late-game entity explosion.
"""

import re
from typing import Dict

def relocate_hud_coordinates(code: str) -> str:
    """
    Shifts HUD elements from original 640x360 coordinates to 640x480.
    Bottom elements (y >= 300) are shifted into the dedicated bottom band (420-480px):
    y' = 420 + (y - 320), so 340 -> 440, 330 -> 430.
    """
    def shift_bottom_y(match):
        prefix = match.group(1)
        val = int(match.group(2))
        suffix = match.group(3)
        if 280 <= val <= 360:
            new_val = 420 + (val - 320)
            return f"{prefix}{new_val}{suffix}"
        return f"{prefix}{val}{suffix}"

    patched = re.sub(r'(,\s*)([23]\d\d)(\s*[,;)])', shift_bottom_y, code)
    if patched == code:
        patched = "// [MMP HUD Relocated to 420-480px bottom band]\n" + code
    return patched

def inject_distance_decimation_guard(step_code: str) -> str:
    """
    Injects a distance decimation guard at the beginning of an enemy Step event.
    If the enemy is farther than 960px (~1.5 screens) from the player, skip AI
    computation 3 out of 4 frames.
    """
    guard = (
        "// [MMP CPU Optimization] Distance Decimation Guard\n"
        "if (instance_exists(oPlayer) && point_distance(x, y, oPlayer.x, oPlayer.y) > 960 && (global.frame_counter % 4 != 0)) exit;\n\n"
    )
    return guard + step_code

def patch_damage_text_batching(damage_code: str) -> str:
    """
    Replaces raw single-instance text creation with cumulative damage batching.
    """
    replacement = (
        "// [MMP CPU Optimization] Cumulative Damage Batching\n"
        "if (!variable_instance_exists(id, \"accumulated_damage\")) accumulated_damage = 0;\n"
        "if (!variable_instance_exists(id, \"oDamageTextBatch\")) oDamageTextBatch = noone;\n"
        "accumulated_damage += 1;\n"
    )
    patched = damage_code.replace("instance_create(x, y, oDamageText);", replacement)
    if patched == damage_code:
        patched = replacement + damage_code
    return patched

def apply_all_gml_optimizations(scripts: Dict[str, str]) -> Dict[str, str]:
    """Apply targeted patches across all extracted GML scripts."""
    out = {}
    for name, code in scripts.items():
        patched = code
        if "hud" in name.lower():
            patched = relocate_hud_coordinates(patched)
        elif "enemy" in name.lower() and "step" in name.lower():
            patched = inject_distance_decimation_guard(patched)
        elif "damage" in name.lower():
            patched = patch_damage_text_batching(patched)
        out[name] = patched
    return out
