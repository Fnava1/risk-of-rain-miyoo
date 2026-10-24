# Implementation Plan: Software Circle Rasterizer & SpatialGrid Collision Rectangle Optimization

**Feature:** Fast native circle rasterization in `sw_renderer.c` and SpatialGrid query for `collision_rectangle` in `vm_builtins.c`.  
**Target:** Miyoo Mini Plus ARM Cortex-A7 (Software Rendering) & Host x86_64 Linux/Windows.  
**Objective:** Eliminate CPU stalls causing drops to 39 FPS during the Wandering Vagrant (`oGiantJelly`) boss fight and improve overall frame stability to a solid 60 FPS.

---

## 1. Problem Statement & Root Cause

1. **Wandering Vagrant Missile Primaries:**
   - In `Alarm_3`, `oGiantJelly` fires volleys of 5 to 10 `oJellyMissile` objects (object 246).
   - In `gml_Object_oJellyMissile_Draw_0`, each missile draws 3 circles per frame:
     - Filled circle (radius ~6)
     - Outline circle (radius 4)
     - Outline circle (radius 2)
   - In `butterscotch/src/renderer.h` (`Renderer_drawEllipseColor`), circles are decomposed into 24-segment polygons, calling `cosf` and `sinf` 24 times per circle.
   - For filled circles, `swDrawTriangle` in `sw_renderer.c` does not rasterize solid triangles; it draws 3 lines per triangle (48 radial lines + 24 perimeter lines = 72 lines per filled circle).
   - Result: 10 missiles create 1,200 line draws, 1,440 trig calls, and heavy pixel overdraw per frame, dropping Miyoo Mini FPS from 60 to ~39.

2. **Unindexed `collision_rectangle`:**
   - In `butterscotch/src/vm_builtins.c`, `builtin_collision_rectangle` performs a linear loop over `runner->instanceSnapshots` rather than querying `SpatialGrid`, unlike `collision_circle`, `collision_line`, and `collision_rectangle_list`.

---

## 2. Proposed Solution

### Unit 1: Native Circle Rasterizer in `sw_renderer.c` & `RendererVtable`
- Add an optional `drawCircleColor` function pointer to `RendererVtable` in `butterscotch/src/renderer.h`:
  ```c
  void (*drawCircleColor)(Renderer* renderer, float cx, float cy, float radius, uint32_t col1, uint32_t col2, float alpha, bool outline);
  ```
- In `Renderer_drawCircleColor` ([renderer.h](file:///c:/DEV/RISKRAIN/butterscotch/src/renderer.h)):
  - If `renderer->vtable->drawCircleColor != NULL`, call it directly.
  - Otherwise, fallback to `Renderer_drawEllipseColor`.
- In `butterscotch/src/sw_renderer.c`:
  - Implement `swDrawCircleColor`:
    - Transform center `(cx, cy)` to screen coordinates using `sw_transform_point`.
    - Compute screen-space radius `sr = radius * fabsf(scaleX)`.
    - If `sr <= 0.0f` or alpha <= 0.0f, return early.
    - If `outline`:
      - Use **Midpoint Circle Algorithm** (Bresenham's circle, 8-way symmetry).
      - Zero trigonometric calls, zero line draws. Directly blend 8 symmetric points per step.
    - If `filled`:
      - Use **Horizontal Scanline Span Filler**.
      - For each scanline $y \in [\lceil scy - sr \rceil, \lfloor scy + sr \rfloor]$ clipped to `[clipY0, clipY1)`:
        - Compute $dx = \sqrt{sr^2 - (y - scy)^2}$.
        - Span from $x_0 = \max(\text{clipX0}, \lfloor scx - dx \rfloor)$ to $x_1 = \min(\text{clipX1} - 1, \lceil scx + dx \rceil)$.
        - Fill or blend the row segment in RGB565.
      - Zero line draws, zero trig calls, exactly 1 write/blend per pixel (no radial overdraw).

### Unit 2: Accelerate `builtin_collision_rectangle` via `SpatialGrid`
- In `butterscotch/src/vm_builtins.c` line 14175 (`builtin_collision_rectangle`):
  - Replace `Runner_pushInstancesForTarget` linear snapshot scan with `SpatialGrid_syncGrid` and `SpatialGrid_prepareQuery(runner, x1, y1, x2, y2, targetObjIndex)` (matching `collision_rectangle_list`).
  - Check only instances in overlapping grid cells.

---

## 3. Implementation Steps

### Task 1: Add `drawCircleColor` to `RendererVtable` and `Renderer_drawCircleColor`
- **File:** `butterscotch/src/renderer.h`
- **Action:**
  - Add `void (*drawCircleColor)(Renderer* renderer, float cx, float cy, float radius, uint32_t col1, uint32_t col2, float alpha, bool outline);` to `RendererVtable`.
  - Update `Renderer_drawCircleColor` to check `if (renderer->vtable->drawCircleColor != NULL)`.

### Task 2: Implement `swDrawCircleColor` in `sw_renderer.c`
- **File:** `butterscotch/src/sw_renderer.c`
- **Action:**
  - Implement midpoint circle algorithm for outline = true.
  - Implement scanline span filler for outline = false.
  - Register `swVtable.drawCircleColor = swDrawCircleColor`.

### Task 3: Accelerate `builtin_collision_rectangle` with `SpatialGrid`
- **File:** `butterscotch/src/vm_builtins.c`
- **Action:**
  - In `builtin_collision_rectangle`, call `SpatialGrid_syncGrid(runner, runner->spatialGrid)` and `SpatialGrid_prepareQuery`.
  - Iterate through grid cells `query.range.minGridX..query.range.maxGridX` and `query.range.minGridY..query.range.maxGridY`.

### Task 4: Compilation, Unit Tests & Miyoo Mini ARM Build
- **Action:**
  - Compile native host Butterscotch binary inside docker.
  - Run python test suite: `python -m unittest discover tests -v` (ensure all 25 tests pass).
  - Cross-compile Miyoo Mini ARM binary (`build_miyoo`), strip it, verify size < 2.5 MB.
  - Assemble distribution package `dist/Risk_of_Rain_MiyooMiniPlus.zip` and verify size < 5 MB.
  - Synchronize Trail context graph: `trail build`.
