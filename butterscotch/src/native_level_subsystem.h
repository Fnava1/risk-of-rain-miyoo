#ifndef _BS_NATIVE_LEVEL_SUBSYSTEM_H_
#define _BS_NATIVE_LEVEL_SUBSYSTEM_H_

#include "common.h"
#include <stdint.h>
#include <stdbool.h>

struct Runner;
struct Instance;

// ===[ Native Console Level Pipeline (NCLP) ]===
// High-performance native subsystems engineered for low-power console ARM CPUs (Miyoo Mini Plus):
// 1. Spatial Sector Hibernation (SSH): Isolates active arenas (e.g., Level 6 Providence Bridge)
//    and hibernates non-arena background instances to free up cache lines and iteration overhead.
// 2. Static Bitmask Collision Bake (SGB): Collapses thousands of static solid blocks (oB, oBNoSpawn)
//    into a dense 1-bit-per-16x16-cell bitmask, evaluating terrain collisions in O(1) in L1 cache.
// 3. Native AOT Entity Hooks (NEH): Native C vector kinematics for multi-segment bosses (Gilded Wurms),
//    bypassing stack-based GML bytecode execution.
// 4. Replicable multi-stage sector profiles for Levels 1 through 5.

void NCLP_init(struct Runner* runner);
void NCLP_onRoomStart(struct Runner* runner);
void NCLP_onRoomEnd(struct Runner* runner);
void NCLP_step(struct Runner* runner);

// Returns true if the event was handled natively in C, completely bypassing VM bytecode execution
bool NCLP_overrideEvent(struct Runner* runner, struct Instance* inst, int32_t eventType, int32_t eventSubtype, int32_t codeId, int32_t ownerObjectIndex);

// Fast O(1) solid terrain check in L1 cache
bool NCLP_checkSolid(struct Runner* runner, float x1, float y1, float x2, float y2);

#endif // _BS_NATIVE_LEVEL_SUBSYSTEM_H_
