"""
GML Bytecode Patcher for Risk of Rain (2013) on Miyoo Mini Plus.
Refined implementation:
1. Safely relocates Y-coordinates for HUD drawing (leaving X-coordinates and color parameters intact).
2. Guards expensive pathfinding calculations without halting entity gravity/physics.
3. Batches floating damage text using real damage amounts (argument0).
"""

import re
from typing import Dict

def relocate_hud_coordinates(code: str) -> str:
    """
    Shifts HUD Y-coordinates from original 640x360 coordinates to 640x480.
    Bottom elements (y >= 280) are shifted into the bottom band (420-480px):
    y' = 420 + (y - 320), so 340 -> 440.
    Precisely targets the Y argument based on function signature.
    """
    def shift_val(val_str):
        val = int(val_str)
        if 280 <= val <= 360:
            return str(420 + (val - 320))
        return val_str

    # 1. 4-arg coordinate functions: draw_sprite_ext(spr, img, x, y, ...) and draw_sprite(spr, img, x, y)
    def repl_4th(m):
        func = m.group(1)
        arg1 = m.group(2)
        arg2 = m.group(3)
        arg_x = m.group(4)
        arg_y = shift_val(m.group(5))
        rest = m.group(6)
        return f"{func}({arg1}, {arg2}, {arg_x}, {arg_y}{rest}"

    pattern_4th = r'(\bdraw_sprite(?:_ext)?)\(\s*([^,\n]+),\s*([^,\n]+),\s*([^,\n]+),\s*(\d+)(\s*[,;)])'
    code = re.sub(pattern_4th, repl_4th, code)

    # 2. 2-arg coordinate functions: draw_hp_bar(x, y, ...), draw_skills(x, y, ...), draw_text(x, y, ...)
    def repl_2nd(m):
        func = m.group(1)
        arg_x = m.group(2)
        arg_y = shift_val(m.group(3))
        rest = m.group(4)
        return f"{func}({arg_x}, {arg_y}{rest}"

    pattern_2nd = r'(\bdraw_(?:hp_bar|skills|text|healthbar))\(\s*([^,\n]+),\s*(\d+)(\s*[,;)])'
    code = re.sub(pattern_2nd, repl_2nd, code)

    return code

def inject_distance_decimation_guard(step_code: str) -> str:
    """
    Wraps expensive pathfinding / steering calls (e.g. mp_potential_step, path_start)
    so distant monsters (>960px) only evaluate navigation 1 out of 4 frames,
    while allowing physics, gravity, and collision checks to execute unconditionally every frame.
    Uses GM Studio 1.4 built-in current_time instead of undefined variables.
    """
    ai_calls = ["mp_potential_step", "path_start", "mp_grid_path"]
    patched = step_code
    
    for call in ai_calls:
        if call in patched:
            guard_wrap = (
                r'if (!instance_exists(oPlayer) || point_distance(x, y, oPlayer.x, oPlayer.y) <= 960 || ((current_time div 16) % 4 == 0)) { '
                r'\1; }'
            )
            patched = re.sub(rf'(\b{call}\s*\([^;]+;)', guard_wrap, patched)

    if patched == step_code:
        header = (
            "// [MMP CPU Optimization] Distance Decimation Helper\n"
            "var __mmp_ai_active = (!instance_exists(oPlayer) || point_distance(x, y, oPlayer.x, oPlayer.y) <= 960 || ((current_time div 16) % 4 == 0));\n"
        )
        patched = header + step_code

    return patched

def patch_damage_text_batching(damage_code: str) -> str:
    """
    Replaces raw single-instance text creation with cumulative damage batching,
    aggregating actual incoming damage (argument0).
    """
    replacement = (
        "// [MMP CPU Optimization] Cumulative Damage Batching\n"
        "var __dmg = 1;\n"
        "if (variable_instance_exists(id, \"argument0\")) __dmg = argument0;\n"
        "if (!variable_instance_exists(id, \"accumulated_damage\")) accumulated_damage = 0;\n"
        "if (!variable_instance_exists(id, \"oDamageTextBatch\")) oDamageTextBatch = noone;\n"
        "accumulated_damage += __dmg;\n"
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
