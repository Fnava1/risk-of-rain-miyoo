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
// 5. Native Damage Text Ring Buffer: zero-allocation emitter and rasterizer.

#define DAMAGE_PARTICLES_CAPACITY 128

typedef struct {
    float x, y;
    float vy;
    uint16_t life;
    uint16_t maxLife;
    int32_t amount;
    uint32_t color;
    float scale;
    bool active;
} NativeDamageParticle;

void NCLP_init(struct Runner* runner);
void NCLP_onRoomStart(struct Runner* runner);
void NCLP_onRoomEnd(struct Runner* runner);
void NCLP_step(struct Runner* runner);

void NCLP_emitDamageText(struct Runner* runner, float x, float y, int32_t amount, uint32_t color);
void NCLP_updateDamageText(struct Runner* runner);
void NCLP_drawDamageText(struct Runner* runner);

// 6. Distance-Aware AI Navigation Decimation
bool NCLP_shouldThrottleNPC(struct Runner* runner, struct Instance* inst);

// 7. Contiguous DoT Status Dispatcher
typedef enum {
    STATUS_BLEED = 0,
    STATUS_BURN = 1,
    STATUS_POISON = 2
} StatusEffectType;

typedef struct {
    int32_t targetInstanceId;
    StatusEffectType type;
    int32_t ticksRemaining;
    float damagePerTick;
    uint32_t timer;
    bool active;
} NativeStatusEffect;

#define MAX_ACTIVE_STATUS_EFFECTS 256

void NCLP_applyStatusEffect(struct Runner* runner, int32_t targetInstanceId, StatusEffectType type, int32_t ticks, float damagePerTick);
void NCLP_processStatusEffects(struct Runner* runner);

// Returns true if the event was handled natively in C, completely bypassing VM bytecode execution
bool NCLP_overrideEvent(struct Runner* runner, struct Instance* inst, int32_t eventType, int32_t eventSubtype, int32_t codeId, int32_t ownerObjectIndex);

// Fast O(1) solid terrain check in L1 cache
bool NCLP_checkSolid(struct Runner* runner, float x1, float y1, float x2, float y2);

// Clear solid terrain cells in bitmask on instance destruction
void NCLP_clearSolid(struct Runner* runner, float x1, float y1, float x2, float y2);

// Fast DDA raycast against solid terrain in L1 cache
bool NCLP_raycastSolid(struct Runner* runner, float x1, float y1, float x2, float y2, float* hitX, float* hitY);

#endif // _BS_NATIVE_LEVEL_SUBSYSTEM_H_
