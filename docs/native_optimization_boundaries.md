# Boundaries for Native C Optimizations in Butterscotch (Risk of Rain)

## Context & Purpose
This document establishes the permanent technical boundaries for native C translation (`NCLP` / Butterscotch Engine) in the *Risk of Rain (2013)* Miyoo Mini Plus port.
While translating hot paths from GML bytecode into native C yields critical performance gains on constrained ARM CPUs (Allwinner V3s / Ingenic JZ4775 @ 1.2 GHz), certain systems **must NEVER be translated to native C** due to gameplay fragility, subtle GML interactions, or lack of frame-rate benefit.

---

## Systems Excluded from Native Translation (DO NOT TOUCH)

### 1. Player State Machine & Class Scripts (`step_player` / ~12.7 KB)
* **Scope:** `gml_Script_step_player`, `activity_*`, survivor combat abilities, roll/dodge states, invulnerability frames, and gamepad input buffering.
* **Why it must remain in GML:**
  - High risk of gameplay desynchronization: each survivor class has unique frame cancellation windows, jump physics nuances, and animation trigger hooks.
  - Interacts heavily with dynamic player variables (`canmove`, `invincible`, `pVspeed`, `pHspeed`, `state`).
  - Rewriting this in C risks breaking ability timings, cancel windows, and survivor-specific item interactions.

### 2. Game Director & Difficulty Scaling (`oDirectorControl` / ~27.6 KB)
* **Scope:** `gml_Object_oDirectorControl_Create_0`, `Other_4`, `Alarm_1`, difficulty points calculation, and stage spawn budgets.
* **Why it must remain in GML:**
  - **Zero per-frame performance cost:** The director runs periodically on alarm ticks (every 1 to 5 seconds), never in the tight per-frame hot loop.
  - Rewriting it in C provides negligible FPS improvements while carrying immense risk of altering the game's iconic difficulty curve, enemy spawn density, or stage boss selection.

### 3. Complex Item Sinergies & Weapon Logic (`use_item`, `object_to_item`, `oExplosion_Collision_325`)
* **Scope:** Active item triggers, item stacking formulas, damage pipeline modifiers (e.g. 56 Leaf Clover, Ceremonial Dagger, Meat Hook, Plasma Rod, Heaven Cracker).
* **Why it must remain in GML:**
  - Contains hundreds of branching conditional cases checking arbitrary dynamic inventory arrays and item counts.
  - GML's dynamic typing allows seamless evaluation of item procs during hit calculations. Hardcoding these rules in C creates high maintenance overhead and risks breaking edge-case item synergies.

### 4. Boss Macro-FSM & Room Cinematic Triggers
* **Scope:** Providence phase transitions (`oProvidence_Step_1`), portal cutscenes, dialog triggers, and teleporter charge event timers.
* **Why it must remain in GML:**
  - Macro FSMs dictate level flow and narrative timing.
  - Only micro-kinematics (such as vector steering for `oWurmBody` segment inverse kinematics or missile homing) should be native in C; state transitions must remain standard GML to guarantee 100% engine compatibility.

---

## Permitted & Recommended Native Optimization Domains
Native C translations (`NCLP_overrideEvent`, `vm_builtins.c`) should be strictly confined to:
1. **Low-level Spatial & Geometric Math:**
   - Solid Grid Bitmask (SGB) terrain rejection (`place_meeting`, `instance_place`, `collision_line`, `collision_point`).
   - Inverse kinematics for chained multi-segment entities (`oWurmBody`).
2. **Volumetric Particle / Particle-like Entity Kinematics:**
   - Gold coins (`oEfGold`), Experience orbs (`oEfExp`), homing missiles (`oEfMissile`).
   - Damage text popups (ring buffer rasterization).
3. **Local Spatial Queries for AI Assistants:**
   - Drone target scanning within local bounded radiuses (`pDrone`, `oDroneDisp`).
