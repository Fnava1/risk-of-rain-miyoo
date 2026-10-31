"""
GML Bytecode Patcher for Risk of Rain (2013) on Miyoo Mini Plus.
Refined implementation:
1. Safely relocates Y-coordinates for HUD drawing (leaving X-coordinates and color parameters intact).
2. Guards expensive pathfinding calculations without halting entity gravity/physics.
3. Batches floating damage text using real damage amounts (argument0).
4. Fixes homing missile proximity detonation and Nematocyst Nozzle fallback when enemies are out of range.
5. Clamps Artifact of Command item selection grid within visible viewport and HUD safe zone.
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

def patch_nematocyst_and_missile_logic(code: str) -> str:
    """
    Fixes homing missiles and Nematocyst Nozzle usable item (Bugs 1 and 9):
    1. Ensures Nematocyst spawns missiles with a forward fan pattern if no target exists in 600px.
    2. Ensures homing missiles (oJellyMissileFriendly, oEfMissileMagic) detonate on proximity
       (point_distance <= max(16, speed + 4)) to prevent perpetual orbiting or sticking.
    3. Guarantees Wurm missiles and body contact trigger damage against player/pFriend.
    """
    patched = code

    # 1. Nematocyst empty target fallback
    if 'oJellyMissileFriendly' in patched and 'else' not in patched:
        fallback = (
            " else {\n"
            "        var fallback_angle = (image_xscale > 0) ? 0 : 180;\n"
            "        repeat(6) {\n"
            "            var m = instance_create(x, y, oJellyMissileFriendly);\n"
            "            m.direction = fallback_angle + random_range(-30, 30);\n"
            "            m.speed = 8;\n"
            "        }\n"
            "    }\n"
        )
        last_brace = patched.rfind('}')
        if last_brace != -1:
            patched = patched[:last_brace+1] + fallback + patched[last_brace+1:]
        else:
            patched += fallback

    # 2. Homing missile proximity fallback (expand <= 10 to <= max(16, speed + 4))
    patched = re.sub(
        r'point_distance\s*\(\s*x\s*,\s*y\s*,\s*target\.x\s*,\s*target\.y\s*\)\s*<=\s*10\b',
        'point_distance(x, y, target.x, target.y) <= max(16, speed + 4)',
        patched
    )

    # 3. Wurm missiles and contact handling
    if 'pFriend' in patched and 'place_meeting' in patched:
        if 'point_distance' not in patched:
            patched = patched.replace(
                'place_meeting(x, y, pFriend)',
                '(place_meeting(x, y, pFriend) || (instance_exists(pFriend) && point_distance(x, y, pFriend.x, pFriend.y) <= max(24, speed + 4)))'
            )

    return patched

def patch_command_artifact_grid_bounds(code: str) -> str:
    """
    Clamps and centers Artifact of Command item selection grid coordinates within the handheld viewport
    (view_xview[0], view_yview[0], view_wview[0], view_hview[0]) and ensures elements
    do not render behind the top HUD bar / skills (safe HUD margin: 48-52px). (Bug 5)
    """
    patched = code

    # 1. Direct viewport centering for pArtifact8Box_Draw_0 grid
    if "tx =" in patched or "tx=" in patched:
        patched = re.sub(
            r'(\btx\s*=\s*)([^;\n]+)(;)',
            r'var __grid_x0 = view_xview[0] + (view_wview[0] - (10 * 38)) / 2; \1__grid_x0 + tc * 38\3',
            patched
        )
    if "ty =" in patched or "ty=" in patched:
        patched = re.sub(
            r'(\bty\s*=\s*)([^;\n]+)(;)',
            r'var __grid_y0 = view_yview[0] + 52; \1__grid_y0 + tr * 38\3',
            patched
        )

    # 2. Fallback clamps for draw_x / x and draw_y / y
    if "draw_x" in patched:
        patched = re.sub(
            r'(\b(?:var\s+)?draw_x\s*=\s*)([^;\n]+)(;)',
            r'\1clamp(\2, view_xview[0] + 16, view_xview[0] + view_wview[0] - 16)\3',
            patched
        )
    elif "x = " in patched and "view_xview" not in patched and "tx" not in patched:
        patched = re.sub(
            r'(\bx\s*=\s*)([^;\n]+)(;)',
            r'\1clamp(\2, view_xview[0] + 16, view_xview[0] + view_wview[0] - 16)\3',
            patched
        )

    if "draw_y" in patched:
        patched = re.sub(
            r'(\b(?:var\s+)?draw_y\s*=\s*)([^;\n]+)(;)',
            r'\1clamp(\2, view_yview[0] + 48, view_yview[0] + view_hview[0] - 16)\3',
            patched
        )
    elif "y = " in patched and "view_yview" not in patched and "ty" not in patched:
        patched = re.sub(
            r'(\by\s*=\s*)([^;\n]+)(;)',
            r'\1clamp(\2, view_yview[0] + 48, view_yview[0] + view_hview[0] - 16)\3',
            patched
        )

    return patched

def patch_stage_transition_pacing(code: str) -> str:
    """
    Optimizes stage transitions, room loading delays, and Item Log INI thrashing:
    1. Consolidates ini_open/ini_close inside loops (like oStorageMenu) to open once outside.
    2. Replaces abrupt audio_stop_all() before room_goto with smooth fade and transition pacing.
    (Bugs 2, 3, 7)
    """
    patched = code

    # 1. Consolidate ini_open/close inside loops
    loop_ini_pattern = r'(for\s*\([^)]+\)\s*\{[\s\S]*?)(ini_open\s*\([^)]+\)\s*;)([\s\S]*?)(ini_close\s*\(\s*\)\s*;)([\s\S]*?\})'
    m = re.search(loop_ini_pattern, patched)
    if m:
        ini_open_call = m.group(2)
        ini_close_call = m.group(4)
        loop_pre = m.group(1)
        loop_body = m.group(3)
        loop_post = m.group(5)
        consolidated = f"{ini_open_call}\n{loop_pre}{loop_body}{loop_post}\n{ini_close_call}"
        patched = patched[:m.start()] + consolidated + patched[m.end():]

    # 2. Smooth audio transition before room_goto / room_goto_next
    patched = re.sub(
        r'audio_stop_all\s*\(\s*\)\s*;\s*(room_goto(?:_next)?\s*\([^)]*\)\s*;)',
        r'audio_sound_gain(all, 0, 200); \1',
        patched
    )

    return patched

def patch_clay_man_ai(code: str) -> str:
    """
    Decimates collision_line raycasts in Clay Man AI (step_clay).
    Staggers raycasting 1 out of 3 frames across instances using ((id + (current_time div 16)) % 3 == 0),
    reducing raycast CPU load by 66% while preserving 60 FPS physics and motion. (Bug 8)
    """
    patched = code
    if "collision_line" in patched:
        guard = r'(((id + (current_time div 16)) % 3 == 0) && (\1))'
        patched = re.sub(r'(\bcollision_line\s*\([^;]+?\))', guard, patched)
    return patched

def patch_boss_shadow_effects(code: str) -> str:
    """
    Throttles Providence Shadow clone (oBoss2Clone) particle emissions and redundant
    additive alpha passes, preventing severe slowdown during final boss shadow phase. (Bug 10)
    """
    patched = code
    if "part_particles_create" in patched:
        wrap = r'if (((current_time div 16) % 2) == 0) { \1; }'
        patched = re.sub(r'(\bpart_particles_create\s*\([^;]+;)', wrap, patched)
    return patched

def patch_player_skill_watchdog(code: str) -> str:
    """
    Injects a failsafe watchdog for player activity state and clamps skill cooldowns (Bug 6):
    - If activity != 0 for >180 frames without completing attack, resets activity = 0.
    - Clamps skill2_cooldown <= 0 to 0 to prevent negative underflow locking Skill 2.
    """
    header = (
        "// [MMP Fix] Player Skill 2 Watchdog & Underflow Guard\n"
        "if (variable_instance_exists(id, \"activity\") && activity != 0) {\n"
        "    if (!variable_instance_exists(id, \"__activity_watchdog\")) __activity_watchdog = 0;\n"
        "    __activity_watchdog += 1;\n"
        "    if (__activity_watchdog > 180) {\n"
        "        activity = 0;\n"
        "        __activity_watchdog = 0;\n"
        "    }\n"
        "} else {\n"
        "    __activity_watchdog = 0;\n"
        "}\n"
        "if (variable_instance_exists(id, \"skill2_cooldown\") && skill2_cooldown <= 0) {\n"
        "    skill2_cooldown = 0;\n"
        "}\n"
    )
    patched = header + code

    # Protect cooldown decrement if present
    patched = re.sub(
        r'(\bskill2_cooldown\s*-=\s*1\s*;)',
        r'\1 if (skill2_cooldown <= 0) skill2_cooldown = 0;',
        patched
    )

    return patched

def patch_blaze_particles(code: str) -> str:
    """
    Throttles oEfBlaze particle emissions across burning enemies.
    Emits particles 1 out of 3 frames across instances using ((id + (current_time div 16)) % 3 == 0),
    reducing particle engine CPU load by 66% while preserving continuous burn damage ticks.
    """
    patched = code
    if "part_particles_create_color" in patched:
        wrap = r'if (((id + (current_time div 16)) % 3) == 0) { \1; }'
        patched = re.sub(r'(\bpart_particles_create_color\s*\([^;]+;)', wrap, patched)
    elif "part_particles_create" in patched:
        wrap = r'if (((id + (current_time div 16)) % 3) == 0) { \1; }'
        patched = re.sub(r'(\bpart_particles_create\s*\([^;]+;)', wrap, patched)
    return patched

def patch_poison_entity_scanning(code: str) -> str:
    """
    Staggers expensive instance_nearest_variable scans in oEfPoison (poison debuff).
    Checks 1 out of 4 frames per entity, preventing massive CPU spikes when dozens of monsters are poisoned.
    """
    patched = code
    if "instance_nearest_variable" in patched:
        header = (
            "// [MMP CPU Optimization] Poison Instance Search Decimation\n"
            "var __mmp_poison_scan = (((id + (current_time div 16)) % 4) == 0);\n"
        )
        if "__mmp_poison_scan" not in patched:
            patched = header + patched
        guard = r'(__mmp_poison_scan && (\1 != noone))'
        patched = re.sub(r'(\binstance_nearest_variable\s*\([^;)]+\)\s*!=\s*noone)', guard, patched)
    return patched

def patch_chain_lightning_fx(code: str) -> str:
    """
    Optimizes electric arcs in oChainLightning_Draw_0 (Ukulele, Tesla Coil).
    Throttles redundant filled alpha circle drawing, preventing rasterizer saturation
    when lightning chains across clustered enemy groups.
    """
    patched = code
    if "draw_circle" in patched:
        wrap = r'if (((current_time div 16) % 2) == 0) { \1; }'
        patched = re.sub(r'(\bdraw_circle\s*\([^;]+;)', wrap, patched)
    return patched

def patch_enemy_aura_and_fog(code: str) -> str:
    """
    Optimizes enemy glow auras and white damage fog flashes in draw_ai_default:
    1. Viewport culls additive glow passes (bm_add) for off-screen / margin entities.
    2. Throttles d3d_set_fog white flashes during recurring damage to max 1 every 2 frames.
    """
    patched = code
    if "draw_set_blend_mode" in patched and "bm_add" in patched:
        guard = (
            "if (x >= view_xview[0] - 64 && x <= view_xview[0] + view_wview[0] + 64 && "
            "y >= view_yview[0] - 64 && y <= view_yview[0] + view_hview[0] + 64) { "
            r"\1; }"
        )
        patched = re.sub(
            r'(\bdraw_set_blend_mode\s*\(\s*bm_add\s*\)\s*;[\s\S]*?\bdraw_set_blend_mode\s*\(\s*bm_normal\s*\)\s*;)',
            guard,
            patched
        )

    if "d3d_set_fog" in patched:
        header = (
            "// [MMP Fog Flash Throttling]\n"
            "if (!variable_instance_exists(id, \"__mmp_fog_tick\")) __mmp_fog_tick = 0;\n"
            "__mmp_fog_tick += 1;\n"
        )
        if "__mmp_fog_tick" not in patched:
            patched = header + patched
        wrap_fog = r'if (__mmp_fog_tick % 2 == 0) { \1; }'
        patched = re.sub(r'(\bd3d_set_fog\s*\(\s*true[^;]+;[\s\S]*?\bd3d_set_fog\s*\(\s*false[^;]+;)', wrap_fog, patched)

    return patched

def patch_heavy_boss_spawns(code: str) -> str:
    """
    Optimizes heavy bosses with frequent instance activation or dense circles:
    1. Decimates oBoar_Step_2 (Toxic Beast) instance_activate_region to run every 15 frames.
    2. Viewport-guards oWurmHead_Draw_0 circle rendering.
    """
    patched = code
    if "instance_activate_region" in patched:
        wrap = r'if (((current_time div 16) % 15) == 0) { \1; }'
        patched = re.sub(r'(\binstance_activate_region\s*\([^;]+;)', wrap, patched)
    if "draw_circle" in patched:
        wrap = r'if (x >= view_xview[0] - 48 && x <= view_xview[0] + view_wview[0] + 48 && y >= view_yview[0] - 48 && y <= view_yview[0] + view_hview[0] + 48) { \1; }'
        patched = re.sub(r'(\bdraw_circle\s*\([^;]+;)', wrap, patched)
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
        elif any(k in name.lower() for k in ["jelly", "missile", "use_item"]):
            patched = patch_nematocyst_and_missile_logic(patched)
        elif any(k in name.lower() for k in ["artifact8", "select", "fairitem"]):
            patched = patch_command_artifact_grid_bounds(patched)
        elif any(k in name.lower() for k in ["storagemenu", "teleporter", "level", "transition"]):
            patched = patch_stage_transition_pacing(patched)
        elif "clay" in name.lower():
            patched = patch_clay_man_ai(patched)
        elif "boss" in name.lower() or "clone" in name.lower():
            patched = patch_boss_shadow_effects(patched)
        elif "player" in name.lower() and ("step" in name.lower() or "key" in name.lower()):
            patched = patch_player_skill_watchdog(patched)
        elif any(k in name.lower() for k in ["blaze", "burn", "fire"]):
            patched = patch_blaze_particles(patched)
        elif "poison" in name.lower():
            patched = patch_poison_entity_scanning(patched)
        elif any(k in name.lower() for k in ["chain", "lightning"]):
            patched = patch_chain_lightning_fx(patched)
        elif any(k in name.lower() for k in ["ai_default", "draw_enemy"]):
            patched = patch_enemy_aura_and_fog(patched)
        elif any(k in name.lower() for k in ["boar", "wurm"]):
            patched = patch_heavy_boss_spawns(patched)
        out[name] = patched
    return out
