# Risk of Rain (2013) — 10 Bugs Fix & Stabilization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve all 10 reported bugs in the Risk of Rain (2013) port for Miyoo Mini / Miyoo Mini Plus, covering projectile collisions, item activation logic, UI viewport clamping, disk I/O caching, stage transition pacing, input state locks, and entity/boss rendering optimizations.

**Architecture:** A hybrid fix leveraging both engine runtime enhancements (in `butterscotch/src/` for spatial collision handling, INI read caching, RGB565 blend acceleration, and input watchdog) and targeted GML bytecode patches (in `tools/gml_patcher.py` for item use fallbacks, Command artifact viewport clamping, Clay Men raycast decimation, damage text batching, and Providence shadow particle throttling).

**Tech Stack:** C (Butterscotch VM runner, SDL 1.2 / Miyoo backend, software rasterizer), Python (GML bytecode patcher, asset pipeline, pytest), GameMaker Studio 1.4 bytecode/format specs.

**Spec:** User bug report with 10 issues (Nematocyst Nozzle & missile homing, Item Log crash/delay, stage transition freeze, damage numbers FPS drops, Command item grid off-screen, Skill 2 lockup, Final stage delay, Clay Man slowdown, Wurm/dagger collision failures, Providence shadow phase 30 FPS).

## Global Constraints
- Clean-room BYOD policy: 0 copyrighted `.win` or `.ogg` assets in git or dist.
- Zero external runtime dependencies: runner remains standalone C/SDL1.2 binary under 2.5 MB ARM executable size.
- 60 FPS target at 640x480 native resolution with 1.5x handheld zoom (426x320 viewport).
- Git policy: local commits only; no `git push`.
- All existing 63 tests in `tests/` must remain passing.

## Review Focus
1. **Missile homing overshoot:** When a homing projectile moves faster than the target collision distance per frame, it must detonate on proximity instead of orbiting indefinitely.
2. **Missing sprite vs mask:** Objects with `sprite_index == -1` but valid `mask_index >= 0` (e.g. `oWurmMissile`) must generate a valid axis-aligned bounding box and register in the spatial collision grid.
3. **MicroSD FAT32 I/O stalls:** Repetitive `ini_open`/`ini_read`/`ini_close` loops (such as `oStorageMenu` scanning 100+ items) must be serviced from an in-memory cache without synchronous SD card reads.
4. **Handheld Viewport Clipping:** UI grids for Artifact of Command (`oSelect`) must dynamically clamp inside the current view bounds `[view_xview + margin, view_yview + hud_offset, view_xview + view_wview - margin, view_yview + view_hview - margin]`.
5. **Input state latching:** Input edge transitions in `miyoo.c` and player cooldown states must have failsafes to guarantee Skill 2 cannot enter a permanent lockup state.

---

### Task 1: Projectile Mask Engine Support & Homing Detonation Overhaul (Bugs 1 & 9)

**Files:**
- Modify: `butterscotch/src/collision.h:28-45`
- Modify: `butterscotch/src/spatial_grid.c:107-115`
- Modify: `tools/gml_patcher.py`
- Test: `tests/test_projectile_and_wurm_collisions.py`

**Interfaces:**
- Consumes: `Instance`, `Sprite`, `DataWin`, `SpatialGrid`
- Produces: `Collision_computeBBox` supporting mask-only instances, `patch_nematocyst_and_missile_logic` in `gml_patcher.py`

- [ ] **Step 1: Write the failing test for mask-only BBox computation and missile proximity detonation**

```python
# tests/test_projectile_and_wurm_collisions.py
import pytest
from tools.gml_patcher import patch_nematocyst_and_missile_logic

def test_nematocyst_nozzle_empty_target_fallback():
    # When no enemy is within 600px, use_item must not skip missile spawning
    gml_code = """
    if (instance_exists(target)) {
        instance_create(x, y, oJellyMissileFriendly);
    }
    """
    patched = patch_nematocyst_and_missile_logic(gml_code)
    assert "oJellyMissileFriendly" in patched
    assert "fallback_angle" in patched or "direction" in patched

def test_homing_missile_proximity_fallback():
    # oJellyMissileFriendly and oEfMissileMagic must detonate on proximity <= speed + 4
    gml_step = """
    if (point_distance(x, y, target.x, target.y) <= 10) {
        fire_explosion(x, y, 9);
        instance_destroy();
    }
    """
    patched = patch_nematocyst_and_missile_logic(gml_step)
    assert "point_distance" in patched
    assert "speed + 4" in patched or "<= 16" in patched
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pytest tests/test_projectile_and_wurm_collisions.py -v`
Expected: FAIL with `ImportError` or `AssertionError`.

- [ ] **Step 3: Implement engine bbox fix and GML patcher rules**

1. In `butterscotch/src/collision.h`:
   In `Collision_getSprite`:
   Ensure that if `inst->maskIndex >= 0`, `dataWin->sprt.sprites[inst->maskIndex]` is used. If `inst->spriteIndex == -1` and `inst->maskIndex == -1`, check if `inst->objectIndex` has an object definition default `textureMaskId` or `spriteId` before returning `nullptr`. If a 1x1 or point collision is needed, return a fallback 4x4 bbox centered on `(inst->x, inst->y)` so spatial grid insertion succeeds (`valid = true`).
2. In `tools/gml_patcher.py`:
   Add `patch_nematocyst_and_missile_logic`:
   - In `use_item` (for Nematocyst Nozzle): Replace the early abort when `target == noone` with a forward fan pattern (6 missiles fired in direction `oP.image_xscale > 0 ? 0 : 180` with fanned trajectories).
   - In `oJellyMissileFriendly_Step_2` and `oEfMissileMagic_Step_2`: Expand the detonation condition:
     `if (!instance_exists(target) || point_distance(x, y, target.x, target.y) <= max(16, speed + 4)) { fire_explosion(...); instance_destroy(); }`
   - In `oWurmHead_Step_2` and `oWurmMissile_Step_2`: Ensure collision/damage triggers reliably against `oP` / `pFriend`.

- [ ] **Step 4: Run test to verify it passes**

Run: `pytest tests/test_projectile_and_wurm_collisions.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add butterscotch/src/collision.h butterscotch/src/spatial_grid.c tools/gml_patcher.py tests/test_projectile_and_wurm_collisions.py
git commit -m "fix(collision): resolve projectile homing stalls, mask-only bbox, and wurm damage (bugs 1, 9)"
```

---

### Task 2: Handheld Viewport Clamping & Command Selection Grid Safe Bounds (Bug 5)

**Files:**
- Modify: `tools/gml_patcher.py`
- Test: `tests/test_command_grid_viewport.py`

**Interfaces:**
- Consumes: GML scripts for `pArtifact8Box`, `oSelect`, `oFairItemButton`
- Produces: `patch_command_artifact_grid_bounds` in `gml_patcher.py`

- [ ] **Step 1: Write the failing test for Command item grid clamping**

```python
# tests/test_command_grid_viewport.py
from tools.gml_patcher import patch_command_artifact_grid_bounds

def test_command_artifact_grid_is_clamped_to_view():
    gml_code = """
    var draw_x = x - total_width / 2;
    var draw_y = y - total_height - 20;
    """
    patched = patch_command_artifact_grid_bounds(gml_code)
    assert "view_xview[0]" in patched
    assert "view_yview[0]" in patched
    assert "clamp(" in patched or "min(" in patched
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pytest tests/test_command_grid_viewport.py -v`
Expected: FAIL with `function not found` or `AssertionError`.

- [ ] **Step 3: Implement `patch_command_artifact_grid_bounds` in `tools/gml_patcher.py`**

1. Detect coordinate calculation in `pArtifact8Box` / `oSelect` step/draw scripts where grid position is assigned.
2. Clamp `draw_x` to `[view_xview[0] + 16, view_xview[0] + view_wview[0] - total_width - 16]`.
3. Clamp `draw_y` below the HUD safe margin: `[view_yview[0] + 48, view_yview[0] + view_hview[0] - total_height - 16]`.
4. Ensure item button interactions (`oSelect` / `oFairItemButton`) match the clamped visual coordinates so touch/controller navigation aligns with the icons.

- [ ] **Step 4: Run test to verify it passes**

Run: `pytest tests/test_command_grid_viewport.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/gml_patcher.py tests/test_command_grid_viewport.py
git commit -m "fix(ui): clamp Command artifact item selection grid to handheld viewport (bug 5)"
```

---

### Task 3: INI Read Caching & Smooth Stage Transitions (Bugs 2, 3 & 7)

**Files:**
- Create: `butterscotch/src/runner_ini.h`
- Create: `butterscotch/src/runner_ini.c`
- Modify: `butterscotch/src/runner.c`
- Modify: `butterscotch/src/vm_builtins.c`
- Test: `tests/test_ini_caching_and_room_transitions.py`

**Interfaces:**
- Consumes: `ini_open`, `ini_read_real`, `ini_read_string`, `ini_close`
- Produces: In-memory cached INI data structure avoiding synchronous SD card re-reads on repeated accesses; room change transition pacing

- [ ] **Step 1: Write the failing test for INI caching and room change transition logic**

```python
# tests/test_ini_caching_and_room_transitions.py
import pytest
from tools.gml_patcher import patch_stage_transition_pacing

def test_storage_menu_ini_loop_optimization():
    # Checks that storage menu does not call ini_open/ini_close per item
    gml_code = """
    for (i = 0; i < 120; i++) {
        ini_open("Save.ini");
        val = ini_read_real("Items", string(i), 0);
        ini_close();
    }
    """
    patched = patch_stage_transition_pacing(gml_code)
    # ini_open should be lifted outside the loop or cached
    assert patched.count('ini_open') <= 1

def test_room_transition_loading_indication():
    gml_code = """
    audio_stop_all();
    room_goto_next();
    """
    patched = patch_stage_transition_pacing(gml_code)
    assert "room_goto_next" in patched
    # Audio should fade or play transition sound rather than hard stop
    assert "audio_sound_gain" in patched or "draw_clear" in patched or "audio_stop_all" not in patched
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pytest tests/test_ini_caching_and_room_transitions.py -v`
Expected: FAIL.

- [ ] **Step 3: Implement INI caching in runner and room transition optimizations**

1. In `butterscotch/src/runner_ini.c` / `runner_ini.h`:
   - Implement an INI cache holding parsed `[section] key=value` pairs in an in-memory hash table.
   - When `ini_open(filename)` is called, if `filename` matches the currently cached file and the file has not been written to, reuse the in-memory cache directly without `fopen` / `fread`.
   - On `ini_close()`, write back only if dirty.
   - This turns 200+ synchronous FAT32 SD card disk reads during Item Log (`oStorageMenu`) into $O(1)$ memory lookups, eliminating the freeze and preventing handle exhaustion crashes.
2. In `butterscotch/src/runner.c` & `tools/gml_patcher.py`:
   - Room transition pacing: before invoking synchronous room generation for `rShip` (the 4000x2000 final level) or stage transitions, render a transition fade frame and maintain audio playback rather than abrupt silence.
   - Optimize spatial grid allocation for `rShip`: compute bucket cell size dynamically based on room width/height to avoid allocating 8,000 empty spatial cells.

- [ ] **Step 4: Run test to verify it passes**

Run: `pytest tests/test_ini_caching_and_room_transitions.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add butterscotch/src/runner_ini.h butterscotch/src/runner_ini.c butterscotch/src/runner.c butterscotch/src/vm_builtins.c tools/gml_patcher.py tests/test_ini_caching_and_room_transitions.py
git commit -m "perf(core): add in-memory INI cache and optimize room transition pacing (bugs 2, 3, 7)"
```

---

### Task 4: Frame-Pacing & Performance Optimization: Damage Numbers, Clay Men, Providence Shadow (Bugs 4, 8 & 10)

**Files:**
- Modify: `tools/gml_patcher.py`
- Modify: `butterscotch/src/runner_render.c`
- Modify: `butterscotch/src/vm_builtins.c`
- Test: `tests/test_performance_optimizations_10bugs.py`

**Interfaces:**
- Consumes: `oEfDamage`, `step_clay`, `oBoss2Clone`
- Produces: Throttled damage text batching, staggered raycasting for Clay Men, optimized additive blending for Providence Shadow

- [ ] **Step 1: Write the failing test for damage text, Clay Men AI, and boss shadow particle limits**

```python
# tests/test_performance_optimizations_10bugs.py
from tools.gml_patcher import patch_damage_text_batching, patch_clay_man_ai, patch_boss_shadow_effects

def test_damage_text_instance_capping():
    code = "instance_create(x, y, oDamageText);"
    patched = patch_damage_text_batching(code)
    assert "instance_number" in patched or "accumulated_damage" in patched

def test_clay_man_raycast_staggering():
    step_code = "if (collision_line(x, y, target.x, target.y, pBlock, false, true) == noone) { move(); }"
    patched = patch_clay_man_ai(step_code)
    # Must decimate raycast frequency using instance ID and time
    assert "%" in patched or "alarm" in patched

def test_boss_shadow_particle_throttling():
    boss_code = "part_particles_create(sys, x, y, pt_Shadow, 1);"
    patched = patch_boss_shadow_effects(boss_code)
    assert "part_particles_create" in patched
    assert "% 2 == 0" in patched or "global.time" in patched
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pytest tests/test_performance_optimizations_10bugs.py -v`
Expected: FAIL.

- [ ] **Step 3: Implement performance optimizations in patcher and runner**

1. In `tools/gml_patcher.py`:
   - `patch_damage_text_batching`: If `instance_number(oEfDamage) > 12`, merge damage numbers on the same enemy or drop floating text for tick damage < 5% max HP while retaining actual damage deduction.
   - `patch_clay_man_ai`: Stagger vision raycasts in `step_clay`: `if ((id + current_time div 16) % 3 == 0) { ...collision_line... }` so raycasts run at 20Hz instead of 60Hz per Clay Man, saving 66% raycast CPU without affecting responsiveness.
   - `patch_boss_shadow_effects`: Throttle Providence Shadow clone (`oBoss2Clone`) particle generation to alternate frames and reduce shadow trail sprite draws from 5 overlapping alpha blits to 2.
2. In `butterscotch/src/runner_render.c`:
   - Implement fast RGB565 additive blending loop (`bm_add`) using 32-bit unpacked pixel math (`(r1+r2) | (g1+g2) | (b1+b2)` with bit-mask clamping), cutting CPU time spent in Providence shadow rendering by >50%.

- [ ] **Step 4: Run test to verify it passes**

Run: `pytest tests/test_performance_optimizations_10bugs.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/gml_patcher.py butterscotch/src/runner_render.c butterscotch/src/vm_builtins.c tests/test_performance_optimizations_10bugs.py
git commit -m "perf(render): batch damage text, throttle clay man raycasts, and accelerate boss shadow blending (bugs 4, 8, 10)"
```

---

### Task 5: Input State Latching Failsafe & Player Activity State Watchdog (Bug 6)

**Files:**
- Modify: `butterscotch/src/backends/miyoo.c`
- Modify: `tools/gml_patcher.py`
- Test: `tests/test_skill_input_watchdog.py`

**Interfaces:**
- Consumes: SDL input events, `activity`, `skill2_cooldown`, `alarm[3]`
- Produces: Auto-reset watchdog for stuck activity/skill states; key-up latching immunity

- [ ] **Step 1: Write the failing test for input watchdog and activity recovery**

```python
# tests/test_skill_input_watchdog.py
from tools.gml_patcher import patch_player_skill_watchdog

def test_player_skill_activity_watchdog_injection():
    step_player_code = """
    // Player step logic
    if (activity == 2) {
        // skill 2 active
    }
    """
    patched = patch_player_skill_watchdog(step_player_code)
    assert "activity_timer" in patched or "skill2_cooldown" in patched
    assert "activity = 0" in patched
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pytest tests/test_skill_input_watchdog.py -v`
Expected: FAIL.

- [ ] **Step 3: Implement watchdog in GML patcher and input state handling in runner**

1. In `butterscotch/src/backends/miyoo.c`:
   - In the input polling loop, ensure button release events (`SDL_KEYUP` / bitmask clearing) are reliably processed even when multiple frames are dropped. If physical button is not held according to `SDL_GetKeyState`, force clear the internal pressed state.
2. In `tools/gml_patcher.py`:
   - Add `patch_player_skill_watchdog`: Inject a failsafe check into `step_player`:
     If `activity != 0` for more than 180 consecutive frames without an active attack animation playing, or if `skill2_cooldown < 0`, reset `activity = 0` and reset `skill2_cooldown = 0`. This guarantees Skill 2 can never remain locked permanently.

- [ ] **Step 4: Run test to verify it passes**

Run: `pytest tests/test_skill_input_watchdog.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add butterscotch/src/backends/miyoo.c tools/gml_patcher.py tests/test_skill_input_watchdog.py
git commit -m "fix(input): add skill activity watchdog and prevent input state latching (bug 6)"
```

---

### Task 6: End-to-End Pipeline Verification & Full Test Suite

**Files:**
- Modify: `tools/assemble_dist.py`
- Test: `tests/` (full test suite)

**Interfaces:**
- Consumes: All updated patch functions and C runner builds
- Produces: Verified distribution package and passing test suite

- [ ] **Step 1: Run complete pytest suite**

Run: `pytest tests/ -v`
Expected: All tests pass (previous 63 + new test suites).

- [ ] **Step 2: Verify patcher integration in `assemble_dist.py`**

Run: `python tools/assemble_dist.py --help`
Verify that new patcher routines are hooked into the build pipeline.

- [ ] **Step 3: Check git status and package clean-room compliance**

Verify:
- No copyrighted files committed (`git status`).
- No changes pushed (`git status` shows master ahead of origin, clean tree).

- [ ] **Step 4: Final commit for integration**

```bash
git add tools/assemble_dist.py
git commit -m "chore(dist): integrate 10-bug fixes into distribution pipeline"
```
