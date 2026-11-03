#include "native_level_subsystem.h"
#include "runner.h"
#include "instance.h"
#include "vm.h"
#include "spatial_grid.h"
#include "collision.h"
#include <math.h>
#include <string.h>

// Static cached variable IDs and object indices
static int32_t s_var_parent_id = -1;
static int32_t s_var_bounces_id = -1;
static int32_t s_var_dead_id = -1;
static int32_t s_var_vsspeed_id = -1;
static int32_t s_var_hsspeed_id = -1;
static int32_t s_var_pgravity_id = -1;
static int32_t s_var_target_id = -1;
static int32_t s_var_hp_id = -1;
static int32_t s_obj_wurm_body = -1;
static int32_t s_obj_wurm_head = -1;
static int32_t s_obj_guard_bullet = -1;
static int32_t s_obj_wurm_missile = -1;
static int32_t s_obj_op = -1;
static int32_t s_obj_ocommand = -1;
static int32_t s_obj_oboss1 = -1;
static int32_t s_obj_oboss2clone = -1;
static int32_t s_obj_oboss3 = -1;
static int32_t s_obj_ocommand_final = -1;
static int32_t s_var_active_id = -1;
static int32_t s_obj_ef_grenade_enemy = -1;
static int32_t s_obj_ef_gold = -1;
static int32_t s_obj_ef_missile = -1;
static int32_t s_obj_drone_disp = -1;
static int32_t s_obj_p_npc = -1;
static int32_t s_obj_ef_exp = -1;

// Native Damage Particle Ring Buffer
static NativeDamageParticle s_damage_particles[DAMAGE_PARTICLES_CAPACITY];
static int32_t s_damage_particle_head = 0;

void NCLP_emitDamageText(Runner* runner, float x, float y, int32_t amount, uint32_t color) {
    (void)runner;
    NativeDamageParticle* p = &s_damage_particles[s_damage_particle_head];
    p->x = x;
    p->y = y;
    p->vy = -1.2f;
    p->life = 35;
    p->maxLife = 35;
    p->amount = amount;
    p->color = color;
    p->scale = 1.0f;
    p->active = true;

    s_damage_particle_head = (s_damage_particle_head + 1) % DAMAGE_PARTICLES_CAPACITY;
}

void NCLP_updateDamageText(Runner* runner) {
    (void)runner;
    for (int32_t i = 0; i < DAMAGE_PARTICLES_CAPACITY; i++) {
        NativeDamageParticle* p = &s_damage_particles[i];
        if (!p->active) continue;

        p->y += p->vy;
        p->vy *= 0.92f;
        if (p->life > 0) {
            p->life--;
            if (p->life == 0) {
                p->active = false;
            }
        }
    }
}

void NCLP_drawDamageText(Runner* runner) {
    if (!runner || !runner->renderer || !runner->renderer->vtable->drawTextColor) return;
    if (!runner->activeViewValid || runner->inGuiPass) return;

    float vx = (float)runner->activeViewX;
    float vy = (float)runner->activeViewY;
    float vw = (float)runner->activeViewW;
    float vh = (float)runner->activeViewH;

    char numBuf[32];
    for (int32_t i = 0; i < DAMAGE_PARTICLES_CAPACITY; i++) {
        NativeDamageParticle* p = &s_damage_particles[i];
        if (!p->active) continue;

        // Viewport frustum culling
        if (p->x < vx - 32.0f || p->x > vx + vw + 32.0f || p->y < vy - 32.0f || p->y > vy + vh + 32.0f) {
            continue;
        }

        float alpha = (p->life < 10) ? ((float)p->life / 10.0f) : 1.0f;
        snprintf(numBuf, sizeof(numBuf), "%d", p->amount);

        uint32_t col = p->color ? p->color : 0xFFFFFF;
        runner->renderer->vtable->drawTextColor(runner->renderer, numBuf, p->x, p->y, p->scale, p->scale, 0.0f,
                                                (int32_t)col, (int32_t)col, (int32_t)col, (int32_t)col, alpha, -1.0f);
    }
}

// Distance-Aware AI Navigation Decimation
bool NCLP_shouldThrottleNPC(Runner* runner, Instance* inst) {
    if (!runner || !inst || !runner->activeViewValid) return false;

    float margin = 160.0f;
    float vx1 = (float)runner->activeViewX - margin;
    float vx2 = (float)(runner->activeViewX + runner->activeViewW) + margin;
    float vy1 = (float)runner->activeViewY - margin;
    float vy2 = (float)(runner->activeViewY + runner->activeViewH) + margin;

    // Inside visible area + safety margin: NEVER throttle (runs at 60 Hz)
    if (inst->x >= vx1 && inst->x <= vx2 && inst->y >= vy1 && inst->y <= vy2) {
        return false;
    }
    // Out of viewport: decimate navigation to 1 out of 4 frames (15 Hz)
    return ((inst->instanceId + runner->frameCount) & 3) != 0;
}

// Contiguous DoT Status Dispatcher
static NativeStatusEffect s_status_effects[MAX_ACTIVE_STATUS_EFFECTS];

void NCLP_applyStatusEffect(Runner* runner, int32_t targetInstanceId, StatusEffectType type, int32_t ticks, float damagePerTick) {
    (void)runner;
    if (targetInstanceId < 0 || ticks <= 0) return;

    // Check for existing effect of same type on this instance to refresh it
    for (int32_t i = 0; i < MAX_ACTIVE_STATUS_EFFECTS; i++) {
        NativeStatusEffect* s = &s_status_effects[i];
        if (s->active && s->targetInstanceId == targetInstanceId && s->type == type) {
            s->ticksRemaining = ticks;
            if (damagePerTick > s->damagePerTick) s->damagePerTick = damagePerTick;
            return;
        }
    }

    // Allocate first free slot
    for (int32_t i = 0; i < MAX_ACTIVE_STATUS_EFFECTS; i++) {
        NativeStatusEffect* s = &s_status_effects[i];
        if (!s->active) {
            s->targetInstanceId = targetInstanceId;
            s->type = type;
            s->ticksRemaining = ticks;
            s->damagePerTick = damagePerTick;
            s->timer = 30; // 30 frames = 0.5s per tick
            s->active = true;
            return;
        }
    }
}

void NCLP_processStatusEffects(Runner* runner) {
    if (!runner || !runner->instancesById) return;

    if (s_var_hp_id < 0 && runner->vmContext) {
        s_var_hp_id = VM_getOrAllocateVarID(runner->vmContext, "hp");
    }

    for (int32_t i = 0; i < MAX_ACTIVE_STATUS_EFFECTS; i++) {
        NativeStatusEffect* s = &s_status_effects[i];
        if (!s->active) continue;

        Instance* tgt = hmget(runner->instancesById, s->targetInstanceId);
        if (!tgt || tgt->destroyed || !tgt->active) {
            s->active = false;
            continue;
        }

        if (s->timer > 0) {
            s->timer--;
        }

        if (s->timer == 0) {
            s->timer = 30;
            s->ticksRemaining--;

            if (s_var_hp_id >= 0) {
                RValue hpVal = Instance_getSelfVar(tgt, s_var_hp_id);
                float curHp = (hpVal.type != RVALUE_UNDEFINED) ? (float)RValue_toReal(hpVal) : 100.0f;
                curHp -= s->damagePerTick;
                Instance_setSelfVar(tgt, s_var_hp_id, RValue_makeReal((GMLReal)curHp));

                uint32_t dotColor = (s->type == STATUS_POISON) ? 0x22DD22 : ((s->type == STATUS_BURN) ? 0x2288FF : 0xAA2222);
                NCLP_emitDamageText(runner, tgt->x, tgt->y - 12.0f, (int32_t)s->damagePerTick, dotColor);

                if (curHp <= 0.0f) {
                    s->active = false;
                    Runner_destroyInstance(runner, tgt, true);
                    continue;
                }
            }

            if (s->ticksRemaining <= 0) {
                s->active = false;
            }
        }
    }
}

void NCLP_init(Runner* runner) {
    if (!runner) return;
    runner->sector_hibernation_active = false;
    runner->current_active_sector = 0;
    memset(s_damage_particles, 0, sizeof(s_damage_particles));
    s_damage_particle_head = 0;
    memset(s_status_effects, 0, sizeof(s_status_effects));
    runner->room_solid_bitmask = nullptr;
    runner->room_bitmask_w = 0;
    runner->room_bitmask_h = 0;
    memset(s_damage_particles, 0, sizeof(s_damage_particles));
    s_damage_particle_head = 0;
    memset(s_status_effects, 0, sizeof(s_status_effects));

    if (runner->assetsByName) {
        s_obj_wurm_body = shget(runner->assetsByName, "oWurmBody");
        s_obj_wurm_head = shget(runner->assetsByName, "oWurmHead");
        s_obj_guard_bullet = shget(runner->assetsByName, "oGuardBullet");
        s_obj_wurm_missile = shget(runner->assetsByName, "oWurmMissile");
        s_obj_op = shget(runner->assetsByName, "oP");
        s_obj_ocommand = shget(runner->assetsByName, "oCommand");
        s_obj_oboss1 = shget(runner->assetsByName, "oBoss1");
        s_obj_oboss2clone = shget(runner->assetsByName, "oBoss2Clone");
        s_obj_oboss3 = shget(runner->assetsByName, "oBoss3");
        s_obj_ocommand_final = shget(runner->assetsByName, "oCommandFinal");
        s_obj_ef_grenade_enemy = shget(runner->assetsByName, "oEfGrenadeEnemy");
        s_obj_ef_gold = shget(runner->assetsByName, "oEfGold");
        s_obj_ef_missile = shget(runner->assetsByName, "oEfMissile");
        s_obj_drone_disp = shget(runner->assetsByName, "oDroneDisp");
        s_obj_p_npc = shget(runner->assetsByName, "pNPC");
        s_obj_ef_exp = shget(runner->assetsByName, "oEfExp");

        runner->nclp_wurmBodyObjIndex = s_obj_wurm_body;
        runner->nclp_wurmHeadObjIndex = s_obj_wurm_head;
        runner->nclp_guardBulletObjIndex = s_obj_guard_bullet;
        runner->nclp_wurmMissileObjIndex = s_obj_wurm_missile;
    }

    if (runner->vmContext) {
        s_var_parent_id = VM_getOrAllocateVarID(runner->vmContext, "parent");
        s_var_bounces_id = VM_getOrAllocateVarID(runner->vmContext, "bounces");
        s_var_dead_id = VM_getOrAllocateVarID(runner->vmContext, "dead");
        s_var_vsspeed_id = VM_getOrAllocateVarID(runner->vmContext, "vsspeed");
        s_var_hsspeed_id = VM_getOrAllocateVarID(runner->vmContext, "hsspeed");
        s_var_pgravity_id = VM_getOrAllocateVarID(runner->vmContext, "p_gravity");
        s_var_target_id = VM_getOrAllocateVarID(runner->vmContext, "target");
        s_var_active_id = VM_getOrAllocateVarID(runner->vmContext, "active");
    }

    logInfo("NCLP: Native Console Level Pipeline initialized (oWurmHead=%d, oGuardBullet=%d, oEfGrenadeEnemy=%d)\n",
            s_obj_wurm_head, s_obj_guard_bullet, s_obj_ef_grenade_enemy);
}

void NCLP_onRoomStart(Runner* runner) {
    if (!runner) return;

    // 1. Free any previous bitmask
    if (runner->room_solid_bitmask) {
        free(runner->room_solid_bitmask);
        runner->room_solid_bitmask = nullptr;
        runner->room_bitmask_w = 0;
        runner->room_bitmask_h = 0;
    }
    runner->sector_hibernation_active = false;
    runner->current_active_sector = 0;
    memset(s_damage_particles, 0, sizeof(s_damage_particles));
    s_damage_particle_head = 0;
    memset(s_status_effects, 0, sizeof(s_status_effects));

    if (!runner->currentRoom) return;

    // Re-verify cached IDs if needed
    if (s_obj_wurm_body < 0 && runner->assetsByName) {
        s_obj_wurm_body = shget(runner->assetsByName, "oWurmBody");
        s_obj_wurm_head = shget(runner->assetsByName, "oWurmHead");
        s_obj_guard_bullet = shget(runner->assetsByName, "oGuardBullet");
        s_obj_wurm_missile = shget(runner->assetsByName, "oWurmMissile");
        s_obj_op = shget(runner->assetsByName, "oP");
        s_obj_ocommand = shget(runner->assetsByName, "oCommand");
        s_obj_oboss1 = shget(runner->assetsByName, "oBoss1");
        s_obj_oboss2clone = shget(runner->assetsByName, "oBoss2Clone");
        s_obj_oboss3 = shget(runner->assetsByName, "oBoss3");
        s_obj_ocommand_final = shget(runner->assetsByName, "oCommandFinal");
        s_obj_ef_grenade_enemy = shget(runner->assetsByName, "oEfGrenadeEnemy");
        s_obj_ef_gold = shget(runner->assetsByName, "oEfGold");
        s_obj_ef_missile = shget(runner->assetsByName, "oEfMissile");
        s_obj_drone_disp = shget(runner->assetsByName, "oDroneDisp");
        s_obj_p_npc = shget(runner->assetsByName, "pNPC");

        runner->nclp_wurmBodyObjIndex = s_obj_wurm_body;
        runner->nclp_wurmHeadObjIndex = s_obj_wurm_head;
        runner->nclp_guardBulletObjIndex = s_obj_guard_bullet;
        runner->nclp_wurmMissileObjIndex = s_obj_wurm_missile;
    }

    if (s_var_parent_id < 0 && runner->vmContext) {
        s_var_parent_id = VM_getOrAllocateVarID(runner->vmContext, "parent");
        s_var_bounces_id = VM_getOrAllocateVarID(runner->vmContext, "bounces");
        s_var_dead_id = VM_getOrAllocateVarID(runner->vmContext, "dead");
        s_var_vsspeed_id = VM_getOrAllocateVarID(runner->vmContext, "vsspeed");
        s_var_hsspeed_id = VM_getOrAllocateVarID(runner->vmContext, "hsspeed");
        s_var_pgravity_id = VM_getOrAllocateVarID(runner->vmContext, "p_gravity");
        s_var_target_id = VM_getOrAllocateVarID(runner->vmContext, "target");
        s_var_active_id = VM_getOrAllocateVarID(runner->vmContext, "active");
    }

    // 2. Build Static Bitmask Collision Bake (SGB)
    uint32_t rw = runner->currentRoom->width;
    uint32_t rh = runner->currentRoom->height;
    if (rw == 0 || rh == 0) return;

    uint32_t gw = (rw + 15) / 16;
    uint32_t gh = (rh + 15) / 16;
    uint32_t totalBits = gw * gh;
    uint32_t totalBytes = (totalBits + 7) / 8;

    runner->room_solid_bitmask = (uint8_t*)safeCalloc(totalBytes, 1);
    runner->room_bitmask_w = gw;
    runner->room_bitmask_h = gh;

    // Scan initial static terrain instances (pBlock, oB, oBNoSpawn, oBNoSpawn2, oBNoSpawn3)
    int32_t instCount = (int32_t)arrlen(runner->instances);
    uint32_t solidBlocksBaked = 0;

    for (int32_t i = 0; i < instCount; i++) {
        Instance* inst = runner->instances[i];
        if (!inst || inst->destroyed || !inst->solid) continue;

        if (runner->dataWin && inst->objectIndex >= 0 && (uint32_t)inst->objectIndex < runner->dataWin->objt.count) {
            const char* oname = runner->dataWin->objt.objects[inst->objectIndex].name;
            if (oname && (strncmp(oname, "oB", 2) == 0 || strncmp(oname, "pBlock", 6) == 0)) {
                InstanceBBox bbox = Collision_computeBBox(runner, inst);
                if (!bbox.valid) continue;

                int32_t minX = (int32_t)bbox.left / 16;
                int32_t maxX = (int32_t)bbox.right / 16;
                int32_t minY = (int32_t)bbox.top / 16;
                int32_t maxY = (int32_t)bbox.bottom / 16;

                if (minX < 0) minX = 0;
                if (minY < 0) minY = 0;
                if ((uint32_t)maxX >= gw) maxX = (int32_t)gw - 1;
                if ((uint32_t)maxY >= gh) maxY = (int32_t)gh - 1;

                for (int32_t cy = minY; cy <= maxY; cy++) {
                    uint32_t rowBase = (uint32_t)cy * gw;
                    for (int32_t cx = minX; cx <= maxX; cx++) {
                        uint32_t bitIdx = rowBase + (uint32_t)cx;
                        runner->room_solid_bitmask[bitIdx / 8] |= (uint8_t)(1u << (bitIdx % 8));
                    }
                }
                solidBlocksBaked++;
            }
        }
    }

    logInfo("NCLP: Static Bitmask Collision Bake complete for room '%s' (%ux%u cells, %u bytes, %u blocks baked)\n",
            runner->currentRoom->name ? runner->currentRoom->name : "unnamed",
            gw, gh, totalBytes, solidBlocksBaked);
}

void NCLP_onRoomEnd(Runner* runner) {
    if (!runner) return;

    // Wake up any hibernated instances before room teardown
    if (runner->sector_hibernation_active) {
        int32_t instCount = (int32_t)arrlen(runner->instances);
        for (int32_t i = 0; i < instCount; i++) {
            Instance* inst = runner->instances[i];
            if (inst && inst->sectorHibernated) {
                inst->active = true;
                inst->sectorHibernated = false;
            }
        }
        runner->sector_hibernation_active = false;
    }

    if (runner->room_solid_bitmask) {
        free(runner->room_solid_bitmask);
        runner->room_solid_bitmask = nullptr;
        runner->room_bitmask_w = 0;
        runner->room_bitmask_h = 0;
    }
}

bool NCLP_checkSolid(Runner* runner, float x1, float y1, float x2, float y2) {
    if (!runner || !runner->room_solid_bitmask) return false;

    uint32_t gw = runner->room_bitmask_w;
    uint32_t gh = runner->room_bitmask_h;
    if (gw == 0 || gh == 0) return false;

    int32_t minX = (int32_t)floorf(x1 / 16.0f);
    int32_t maxX = (int32_t)floorf(x2 / 16.0f);
    int32_t minY = (int32_t)floorf(y1 / 16.0f);
    int32_t maxY = (int32_t)floorf(y2 / 16.0f);

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if ((uint32_t)maxX >= gw) maxX = (int32_t)gw - 1;
    if ((uint32_t)maxY >= gh) maxY = (int32_t)gh - 1;

    for (int32_t cy = minY; cy <= maxY; cy++) {
        uint32_t rowBase = (uint32_t)cy * gw;
        for (int32_t cx = minX; cx <= maxX; cx++) {
            uint32_t bitIdx = rowBase + (uint32_t)cx;
            if (runner->room_solid_bitmask[bitIdx / 8] & (uint8_t)(1u << (bitIdx % 8))) {
                return true;
            }
        }
    }
    return false;
}

void NCLP_clearSolid(Runner* runner, float x1, float y1, float x2, float y2) {
    if (!runner || !runner->room_solid_bitmask) return;

    uint32_t gw = runner->room_bitmask_w;
    uint32_t gh = runner->room_bitmask_h;
    if (gw == 0 || gh == 0) return;

    int32_t minX = (int32_t)floorf(x1 / 16.0f);
    int32_t maxX = (int32_t)floorf(x2 / 16.0f);
    int32_t minY = (int32_t)floorf(y1 / 16.0f);
    int32_t maxY = (int32_t)floorf(y2 / 16.0f);

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if ((uint32_t)maxX >= gw) maxX = (int32_t)gw - 1;
    if ((uint32_t)maxY >= gh) maxY = (int32_t)gh - 1;

    for (int32_t cy = minY; cy <= maxY; cy++) {
        uint32_t rowBase = (uint32_t)cy * gw;
        float cellCenterY = (float)cy * 16.0f + 8.0f;
        for (int32_t cx = minX; cx <= maxX; cx++) {
            float cellCenterX = (float)cx * 16.0f + 8.0f;
            uint32_t bitIdx = rowBase + (uint32_t)cx;

            // Check if any other active solid block still occupies this cell in the spatial grid
            bool stillOccupied = false;
            if (runner->spatialGrid) {
                SpatialGridRange range = SpatialGrid_computeCellRange(runner->spatialGrid, cellCenterX, cellCenterY, cellCenterX, cellCenterY);
                for (int32_t gx = range.minGridX; gx <= range.maxGridX && !stillOccupied; gx++) {
                    for (int32_t gy = range.minGridY; gy <= range.maxGridY && !stillOccupied; gy++) {
                        Instance** cell = runner->spatialGrid->grid[SpatialGrid_cellIndex(runner->spatialGrid, gx, gy)];
                        int32_t cellLen = (int32_t)arrlen(cell);
                        for (int32_t ci = 0; ci < cellLen; ci++) {
                            Instance* other = cell[ci];
                            if (other && other->active && !other->destroyed && other->solid) {
                                stillOccupied = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (!stillOccupied) {
                runner->room_solid_bitmask[bitIdx / 8] &= (uint8_t)~(1u << (bitIdx % 8));
            }
        }
    }
}

bool NCLP_raycastSolid(Runner* runner, float x1, float y1, float x2, float y2, float* hitX, float* hitY) {
    if (!runner || !runner->room_solid_bitmask) return false;
    uint32_t gw = runner->room_bitmask_w;
    uint32_t gh = runner->room_bitmask_h;
    if (gw == 0 || gh == 0) return false;

    float dx = x2 - x1;
    float dy = y2 - y1;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 0.001f) {
        if (NCLP_checkSolid(runner, x1, y1, x1, y1)) {
            if (hitX) *hitX = x1;
            if (hitY) *hitY = y1;
            return true;
        }
        return false;
    }

    // Step size: 8 pixels (half a 16px tile, ensuring no tile is skipped)
    int32_t steps = (int32_t)ceilf(dist / 8.0f);
    if (steps > 2000) steps = 2000;
    float stepX = dx / (float)steps;
    float stepY = dy / (float)steps;

    float curX = x1;
    float curY = y1;
    for (int32_t i = 0; i <= steps; i++) {
        int32_t cx = (int32_t)curX / 16;
        int32_t cy = (int32_t)curY / 16;
        if (cx >= 0 && cy >= 0 && (uint32_t)cx < gw && (uint32_t)cy < gh) {
            uint32_t bitIdx = (uint32_t)cy * gw + (uint32_t)cx;
            if (runner->room_solid_bitmask[bitIdx / 8] & (uint8_t)(1u << (bitIdx % 8))) {
                if (hitX) *hitX = curX;
                if (hitY) *hitY = curY;
                return true;
            }
        }
        curX += stepX;
        curY += stepY;
    }
    return false;
}

void NCLP_step(Runner* runner) {
    if (!runner) return;
    NCLP_updateDamageText(runner);
    NCLP_processStatusEffects(runner);
    if (!runner->currentRoom || !runner->currentRoom->name) return;

    // Check if we are in r6_1_1 (UES Contact Light, Room 41)
    bool is_contact_light = (strncmp(runner->currentRoom->name, "r6_1_1", 6) == 0 || runner->currentRoomIndex == 41);

    if (is_contact_light) {
        if (s_obj_op < 0 && runner->assetsByName) {
            s_obj_op = shget(runner->assetsByName, "oP");
            s_obj_ocommand = shget(runner->assetsByName, "oCommand");
            s_obj_oboss1 = shget(runner->assetsByName, "oBoss1");
            s_obj_oboss2clone = shget(runner->assetsByName, "oBoss2Clone");
            s_obj_oboss3 = shget(runner->assetsByName, "oBoss3");
            s_obj_ocommand_final = shget(runner->assetsByName, "oCommandFinal");
        }
        if (s_var_active_id < 0 && runner->vmContext) {
            s_var_active_id = VM_getOrAllocateVarID(runner->vmContext, "active");
        }
        if (s_obj_op < 0 || (uint32_t)s_obj_op >= runner->dataWin->objt.count) return;

        Instance** playerList = runner->instancesByObject[s_obj_op];
        if (!playerList || arrlen(playerList) == 0 || !playerList[0] || playerList[0]->destroyed) return;
        Instance* p = playerList[0];

        // Providence Arena & Battle Detection:
        // Sector Hibernation and Cargo Tile Pre-filtering MUST ONLY activate when the battle has actually begun
        // (i.e. terminal oCommand activated inside the cabin, or Providence alive).
        // It must NEVER activate at Door 2 (x=4160) so the ship remains fully visible before the battle starts.
        bool battle_started = false;

        // 1. Check if the bridge cabin terminal (oCommand) was activated
        if (s_obj_ocommand >= 0 && (uint32_t)s_obj_ocommand < runner->dataWin->objt.count) {
            Instance** cmdList = runner->instancesByObject[s_obj_ocommand];
            if (cmdList && arrlen(cmdList) > 0 && cmdList[0] && !cmdList[0]->destroyed) {
                Instance* cmd = cmdList[0];
                if (s_var_active_id >= 0) {
                    RValue val = IntRValueHashMap_get(&cmd->selfVars, s_var_active_id);
                    if (RValue_toReal(val) > 0.5 || RValue_toBool(val)) {
                        battle_started = true;
                    }
                }
            }
        }

        // 2. Check if Providence boss entity is active (Phases 1, 2, 3)
        if (!battle_started) {
            if (s_obj_oboss1 >= 0 && (uint32_t)s_obj_oboss1 < runner->dataWin->objt.count) {
                Instance** list = runner->instancesByObject[s_obj_oboss1];
                if (list && arrlen(list) > 0 && list[0] && !list[0]->destroyed) {
                    battle_started = true;
                }
            }
        }
        if (!battle_started) {
            if (s_obj_oboss2clone >= 0 && (uint32_t)s_obj_oboss2clone < runner->dataWin->objt.count) {
                Instance** list = runner->instancesByObject[s_obj_oboss2clone];
                if (list && arrlen(list) > 0 && list[0] && !list[0]->destroyed) {
                    battle_started = true;
                }
            }
        }
        if (!battle_started) {
            if (s_obj_oboss3 >= 0 && (uint32_t)s_obj_oboss3 < runner->dataWin->objt.count) {
                Instance** list = runner->instancesByObject[s_obj_oboss3];
                if (list && arrlen(list) > 0 && list[0] && !list[0]->destroyed) {
                    battle_started = true;
                }
            }
        }

        // 3. Check if final post-Providence escape has begun
        bool final_escape = false;
        if (s_obj_ocommand_final >= 0 && (uint32_t)s_obj_ocommand_final < runner->dataWin->objt.count) {
            Instance** list = runner->instancesByObject[s_obj_ocommand_final];
            if (list && arrlen(list) > 0 && list[0] && !list[0]->destroyed) {
                final_escape = true;
            }
        }

        bool in_providence_arena = (p->x >= 4150.0f);
        bool in_providence_fight = battle_started && !final_escape && in_providence_arena;

        if (in_providence_fight && !runner->sector_hibernation_active) {
            // Activate Sector Hibernation for Cargo Hold and non-bridge Decks (x < 4100)
            runner->sector_hibernation_active = true;
            runner->drawableListStructureDirty = true;
            int32_t instCount = (int32_t)arrlen(runner->instances);
            int32_t hibernatedCount = 0;

            for (int32_t i = 0; i < instCount; i++) {
                Instance* inst = runner->instances[i];
                if (!inst || inst->destroyed || !inst->active) continue;

                // Never hibernate player or drones
                if (inst == p || inst->objectIndex == s_obj_op) continue;

                // Never hibernate arena instances
                if (inst->x >= 4100.0f) continue;

                // Never hibernate invisible engine controllers (spriteIndex < 0)
                if (inst->spriteIndex < 0) continue;

                // Check object name for controllers or essential subsystems
                if (runner->dataWin && inst->objectIndex >= 0 && (uint32_t)inst->objectIndex < runner->dataWin->objt.count) {
                    const char* oname = runner->dataWin->objt.objects[inst->objectIndex].name;
                    if (oname) {
                        if (strncmp(oname, "pDrone", 6) == 0 || strncmp(oname, "oDrone", 6) == 0) continue;
                        if (strncmp(oname, "oControl", 8) == 0 || strncmp(oname, "pControl", 8) == 0) continue;
                        if (strncmp(oname, "oHUD", 4) == 0 || strncmp(oname, "oDirector", 9) == 0) continue;
                        if (strncmp(oname, "oMusic", 6) == 0 || strncmp(oname, "oSound", 6) == 0) continue;
                        if (strncmp(oname, "oCamera", 7) == 0 || strncmp(oname, "oPause", 6) == 0) continue;
                        if (strncmp(oname, "pRecover", 8) == 0 || strncmp(oname, "oTeleporterFake", 15) == 0 || strncmp(oname, "oBase", 5) == 0) continue;
                    }
                }

                // Never hibernate recovery anchors (pRecover object 46 or its descendants)
                if (inst->objectIndex == 46 || (runner->dataWin && inst->objectIndex >= 0 && (uint32_t)inst->objectIndex < runner->dataWin->objt.count && runner->dataWin->objt.objects[inst->objectIndex].parentId == 46)) {
                    continue;
                }

                bool is_cargo_ambient = false;
                if (runner->dataWin && inst->objectIndex >= 0 && (uint32_t)inst->objectIndex < runner->dataWin->objt.count) {
                    const char* oname = runner->dataWin->objt.objects[inst->objectIndex].name;
                    if (oname) {
                        if (strcmp(oname, "oRainSplash") == 0 || strcmp(oname, "oScreen") == 0 ||
                            strncmp(oname, "oBarrel", 7) == 0 || strncmp(oname, "oChest", 6) == 0 ||
                            strncmp(oname, "oWaterfall", 10) == 0 || strncmp(oname, "oLavafall", 9) == 0 ||
                            strncmp(oname, "oGeyser", 7) == 0 || strncmp(oname, "oSpawn", 6) == 0 ||
                            strncmp(oname, "oMedcab", 7) == 0 || strncmp(oname, "oMedbay", 7) == 0 ||
                            strncmp(oname, "oGauss", 6) == 0 || strncmp(oname, "oHologram", 9) == 0 ||
                            strncmp(oname, "oEfGold", 7) == 0 || strncmp(oname, "oEfExp", 6) == 0 ||
                            strncmp(oname, "oEfDust", 7) == 0) {
                            is_cargo_ambient = true;
                        }
                    }
                }

                // ONLY hibernate non-boss enemies! NEVER hibernate terrain, solids, platforms, chests, items, ropes
                if (!is_cargo_ambient) {
                    if (!runner->isEnemyObject || !runner->isEnemyObject[inst->objectIndex] || (runner->isBossObject && runner->isBossObject[inst->objectIndex])) continue;
                }
                if (inst->solid) continue;

                // Hibernate non-arena instance!
                inst->active = false;
                inst->sectorHibernated = true;
                hibernatedCount++;
            }

            logInfo("NCLP: Activated Sector Hibernation for r6_1_1 Providence Arena (%d non-arena instances hibernated).\n", hibernatedCount);
        } else if (runner->sector_hibernation_active && !in_providence_fight) {
            // Boss fight ended, escape started, or player exited arena: wake up all hibernated instances and restore full tile view!
            runner->sector_hibernation_active = false;
            runner->drawableListStructureDirty = true;
            int32_t instCount = (int32_t)arrlen(runner->instances);
            for (int32_t i = 0; i < instCount; i++) {
                Instance* inst = runner->instances[i];
                if (inst && inst->sectorHibernated) {
                    inst->active = true;
                    inst->sectorHibernated = false;
                }
            }
            logInfo("NCLP: Deactivated Sector Hibernation for r6_1_1 (restored full ship environment).\n");
        } else if (runner->sector_hibernation_active) {
            // Periodic sanity reinforcement: every 30 frames, ensure any instance marked sectorHibernated remains inactive,
            // and hibernate any new non-boss enemies spawned in the cargo hold (x < 4100) while the boss fight runs.
            static uint32_t s_hib_check_counter = 0;
            if (++s_hib_check_counter >= 30) {
                s_hib_check_counter = 0;
                int32_t instCount = (int32_t)arrlen(runner->instances);
                for (int32_t i = 0; i < instCount; i++) {
                    Instance* inst = runner->instances[i];
                    if (!inst || inst->destroyed) continue;
                    if (inst->sectorHibernated && inst->active) {
                        inst->active = false;
                    } else if (!inst->sectorHibernated && inst->active && inst->x < 4100.0f) {
                        if (runner->isEnemyObject && runner->isEnemyObject[inst->objectIndex] &&
                            (!runner->isBossObject || !runner->isBossObject[inst->objectIndex]) &&
                            !inst->solid) {
                            inst->active = false;
                            inst->sectorHibernated = true;
                        }
                    }
                }
            }
        }
    }
}

bool NCLP_overrideEvent(Runner* runner, Instance* inst, int32_t eventType, int32_t eventSubtype, int32_t codeId, int32_t ownerObjectIndex) {
    (void)codeId;
    (void)ownerObjectIndex;
    if (!runner || !inst) return false;

    // 1. Spite Bombs (oEfGrenadeEnemy End Step Fast Bitmask Bounce)
    // Preserves 100% of bombs with exact friction reflection and O(1) bitmask collision in L1 cache
    if (s_obj_ef_grenade_enemy >= 0 && inst->objectIndex == s_obj_ef_grenade_enemy) {
        if (eventType == EVENT_STEP && eventSubtype == STEP_END) {
            inst->imageAngle += 3.0f;

            if (runner->room_solid_bitmask && NCLP_checkSolid(runner, inst->x - 4.0f, inst->y - 4.0f, inst->x + 4.0f, inst->y + 4.0f)) {
                inst->x = inst->xprevious;
                inst->y = inst->yprevious;

                bool hitH = NCLP_checkSolid(runner, inst->x + inst->hspeed - 4.0f, inst->y - 4.0f, inst->x + inst->hspeed + 4.0f, inst->y + 4.0f);
                bool hitV = NCLP_checkSolid(runner, inst->x - 4.0f, inst->y + inst->vspeed - 4.0f, inst->x + 4.0f, inst->y + inst->vspeed + 4.0f);

                if (hitH) inst->hspeed = -inst->hspeed * 0.8f;
                if (hitV) inst->vspeed = -inst->vspeed * 0.6f;
                if (!hitH && !hitV) {
                    inst->hspeed = -inst->hspeed * 0.8f;
                    inst->vspeed = -inst->vspeed * 0.6f;
                }
                Instance_computeSpeedFromComponents(inst);

                if (s_var_bounces_id < 0 && runner->vmContext) {
                    s_var_bounces_id = VM_getOrAllocateVarID(runner->vmContext, "bounces");
                }
                int32_t bVal = 1;
                if (s_var_bounces_id >= 0) {
                    RValue bValVal = Instance_getSelfVar(inst, s_var_bounces_id);
                    bVal = (bValVal.type != RVALUE_UNDEFINED) ? RValue_toInt32(bValVal) + 1 : 1;
                    Instance_setSelfVar(inst, s_var_bounces_id, RValue_makeReal((GMLReal)bVal));
                }

                if (bVal > 2) {
                    Runner_destroyInstance(runner, inst, true);
                }
                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            }
            return true;
        }
    }

    // 2. Gold Coins (oEfGold End Step Fast Kinematics)
    if (s_obj_ef_gold >= 0 && inst->objectIndex == s_obj_ef_gold) {
        if (eventType == EVENT_STEP && eventSubtype == STEP_END) {
            if (s_var_dead_id < 0 && runner->vmContext) {
                s_var_dead_id = VM_getOrAllocateVarID(runner->vmContext, "dead");
                s_var_vsspeed_id = VM_getOrAllocateVarID(runner->vmContext, "vsspeed");
                s_var_hsspeed_id = VM_getOrAllocateVarID(runner->vmContext, "hsspeed");
                s_var_pgravity_id = VM_getOrAllocateVarID(runner->vmContext, "p_gravity");
                s_var_target_id = VM_getOrAllocateVarID(runner->vmContext, "target");
            }
            RValue* deadSlot = IntRValueHashMap_findSlot(&inst->selfVars, s_var_dead_id);
            int32_t isDead = deadSlot ? RValue_toInt32(*deadSlot) : 0;
            if (!isDead) {
                RValue* vssSlot = IntRValueHashMap_findSlot(&inst->selfVars, s_var_vsspeed_id);
                RValue* hssSlot = IntRValueHashMap_findSlot(&inst->selfVars, s_var_hsspeed_id);
                RValue* gravSlot = IntRValueHashMap_findSlot(&inst->selfVars, s_var_pgravity_id);
                float p_grav = gravSlot ? (float)RValue_toReal(*gravSlot) : 0.2f;
                float vss = vssSlot ? (float)RValue_toReal(*vssSlot) + p_grav : p_grav;
                float hss = hssSlot ? (float)RValue_toReal(*hssSlot) : 0.0f;

                inst->x = roundf(inst->x);
                inst->y = roundf(inst->y);

                if (runner->room_solid_bitmask && NCLP_checkSolid(runner, inst->x, inst->y + 4.0f, inst->x, inst->y + 4.0f)) {
                    Instance_setSelfVar(inst, s_var_dead_id, RValue_makeReal(1.0));
                    Instance_setSelfVar(inst, s_var_vsspeed_id, RValue_makeReal(0.0));
                    Instance_setSelfVar(inst, s_var_hsspeed_id, RValue_makeReal(0.0));
                    inst->alarm[1] = 120;

                    // Set target to player if not set
                    RValue tgtVal = Instance_getSelfVar(inst, s_var_target_id);
                    if (tgtVal.type == RVALUE_UNDEFINED || RValue_toInt32(tgtVal) == -5) {
                        if (s_obj_op >= 0 && (uint32_t)s_obj_op < runner->dataWin->objt.count) {
                            Instance** pList = runner->instancesByObject[s_obj_op];
                            if (pList && arrlen(pList) > 0 && pList[0] && !pList[0]->destroyed) {
                                Instance_setSelfVar(inst, s_var_target_id, RValue_makeReal((GMLReal)pList[0]->instanceId));
                            }
                        }
                    }
                } else {
                    Instance_setSelfVar(inst, s_var_vsspeed_id, RValue_makeReal((GMLReal)vss));
                    inst->x += hss;
                    inst->y += vss;
                }
                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            } else {
                inst->speed = 0.0f;
                inst->hspeed = 0.0f;
                inst->vspeed = 0.0f;
            }
            return true;
        }
    }

    // 3. Guided Missiles (oEfMissile End Step Vector Steering)
    if (s_obj_ef_missile >= 0 && inst->objectIndex == s_obj_ef_missile) {
        if (eventType == EVENT_STEP && eventSubtype == STEP_END) {
            if (s_var_target_id < 0 && runner->vmContext) {
                s_var_target_id = VM_getOrAllocateVarID(runner->vmContext, "target");
            }
            if (s_var_target_id >= 0) {
                RValue* tgtSlot = IntRValueHashMap_findSlot(&inst->selfVars, s_var_target_id);
                if (tgtSlot) {
                    int32_t tgtId = RValue_toInt32(*tgtSlot);
                    if (tgtId >= 0 && runner->instancesById) {
                        Instance* tgtInst = hmget(runner->instancesById, tgtId);
                        if (tgtInst && !tgtInst->destroyed) {
                            inst->speed = fminf(inst->speed + 0.2f, 7.0f);
                            float dx = tgtInst->x - inst->x;
                            float dy = tgtInst->y - inst->y;
                            float targetDir = atan2f(-dy, dx) * (180.0f / 3.14159265f);
                            if (targetDir < 0.0f) targetDir += 360.0f;

                            float diff = targetDir - inst->direction;
                            while (diff < -180.0f) diff += 360.0f;
                            while (diff > 180.0f) diff -= 360.0f;
                            float maxTurn = 15.0f;
                            if (diff > maxTurn) diff = maxTurn;
                            else if (diff < -maxTurn) diff = -maxTurn;
                            inst->direction += diff;
                            inst->imageAngle = (GMLReal)inst->direction;
                            Instance_computeComponentsFromSpeed(inst);
                            SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
                            return true;
                        }
                    }
                }
            }
        }
    }

    // 4. Temple Guardian Projectiles (oGuardBullet lifecycle preserved in GML to ensure distance check & self-destruction)
    // Overriding STEP_END for oGuardBullet suppressed point_distance(x,y,target_x,target_y) <= 10 -> instance_destroy(),
    // creating immortal zombie projectiles with frozen white targeting circles and glowing bullets on screen.

    // 5. Allied Drones Target Scanner Acceleration (lifecycle preserved in canonical GML)

    // 6. Experience Orbs (oEfExp Alarm 1 Native Vector Kinematics)
    if (s_obj_ef_exp >= 0 && inst->objectIndex == s_obj_ef_exp) {
        if (eventType == EVENT_ALARM && eventSubtype == 1) {
            inst->alarm[1] = 1;

            if (s_var_target_id < 0 && runner->vmContext) {
                s_var_target_id = VM_getOrAllocateVarID(runner->vmContext, "target");
            }

            Instance* tgtInst = nullptr;
            if (s_var_target_id >= 0) {
                RValue* tgtSlot = IntRValueHashMap_findSlot(&inst->selfVars, s_var_target_id);
                if (tgtSlot) {
                    int32_t tid = RValue_toInt32(*tgtSlot);
                    if (tid >= 0 && runner->instancesById) {
                        tgtInst = hmget(runner->instancesById, tid);
                    }
                }
            }
            if (!tgtInst || tgtInst->destroyed) {
                if (s_obj_op >= 0 && (uint32_t)s_obj_op < runner->dataWin->objt.count) {
                    Instance** pList = runner->instancesByObject[s_obj_op];
                    if (pList && arrlen(pList) > 0 && pList[0] && !pList[0]->destroyed) {
                        tgtInst = pList[0];
                        if (s_var_target_id >= 0) {
                            Instance_setSelfVar(inst, s_var_target_id, RValue_makeReal((GMLReal)tgtInst->instanceId));
                        }
                    }
                }
            }

            if (tgtInst && !tgtInst->destroyed) {
                float dx = tgtInst->x - inst->x;
                float dy = tgtInst->y - inst->y;
                float tdis = sqrtf(dx * dx + dy * dy);
                float td = atan2f(-dy, dx) * (180.0f / 3.14159265f);
                if (td < 0.0f) td += 360.0f;

                inst->speed = fminf(inst->speed + 0.35f, 10.0f);
                if (tdis < 24.0f) {
                    inst->direction = td;
                } else {
                    float diff = td - inst->direction;
                    while (diff < -180.0f) diff += 360.0f;
                    while (diff > 180.0f) diff -= 360.0f;
                    float maxTurn = 18.0f;
                    if (diff > maxTurn) diff = maxTurn;
                    else if (diff < -maxTurn) diff = -maxTurn;
                    inst->direction += diff;
                }
                Instance_computeComponentsFromSpeed(inst);
                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            }
            return true;
        }
    }

    return false;
}
