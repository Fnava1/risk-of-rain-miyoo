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
static int32_t s_obj_wurm_body = -1;
static int32_t s_obj_wurm_head = -1;
static int32_t s_obj_guard_bullet = -1;
static int32_t s_obj_wurm_missile = -1;
static int32_t s_obj_op = -1;

void NCLP_init(Runner* runner) {
    if (!runner) return;
    runner->sector_hibernation_active = false;
    runner->current_active_sector = 0;
    runner->room_solid_bitmask = nullptr;
    runner->room_bitmask_w = 0;
    runner->room_bitmask_h = 0;

    if (runner->assetsByName) {
        s_obj_wurm_body = shget(runner->assetsByName, "oWurmBody");
        s_obj_wurm_head = shget(runner->assetsByName, "oWurmHead");
        s_obj_guard_bullet = shget(runner->assetsByName, "oGuardBullet");
        s_obj_wurm_missile = shget(runner->assetsByName, "oWurmMissile");
        s_obj_op = shget(runner->assetsByName, "oP");

        runner->nclp_wurmBodyObjIndex = s_obj_wurm_body;
        runner->nclp_wurmHeadObjIndex = s_obj_wurm_head;
        runner->nclp_guardBulletObjIndex = s_obj_guard_bullet;
        runner->nclp_wurmMissileObjIndex = s_obj_wurm_missile;
    }

    if (runner->vmContext) {
        s_var_parent_id = VM_getOrAllocateVarID(runner->vmContext, "parent");
    }

    logInfo("NCLP: Native Console Level Pipeline initialized (oWurmBody=%d, oGuardBullet=%d, varParent=%d)\n",
            s_obj_wurm_body, s_obj_guard_bullet, s_var_parent_id);
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

    if (!runner->currentRoom) return;

    // Re-verify cached IDs if needed
    if (s_obj_wurm_body < 0 && runner->assetsByName) {
        s_obj_wurm_body = shget(runner->assetsByName, "oWurmBody");
        s_obj_wurm_head = shget(runner->assetsByName, "oWurmHead");
        s_obj_guard_bullet = shget(runner->assetsByName, "oGuardBullet");
        s_obj_wurm_missile = shget(runner->assetsByName, "oWurmMissile");
        s_obj_op = shget(runner->assetsByName, "oP");

        runner->nclp_wurmBodyObjIndex = s_obj_wurm_body;
        runner->nclp_wurmHeadObjIndex = s_obj_wurm_head;
        runner->nclp_guardBulletObjIndex = s_obj_guard_bullet;
        runner->nclp_wurmMissileObjIndex = s_obj_wurm_missile;
    }

    if (s_var_parent_id < 0 && runner->vmContext) {
        s_var_parent_id = VM_getOrAllocateVarID(runner->vmContext, "parent");
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

void NCLP_step(Runner* runner) {
    if (!runner || !runner->currentRoom || !runner->currentRoom->name) return;

    // Check if we are in r6_1_1 (UES Contact Light, Room 41)
    bool is_contact_light = (strncmp(runner->currentRoom->name, "r6_1_1", 6) == 0 || runner->currentRoomIndex == 41);

    if (is_contact_light) {
        if (s_obj_op < 0 && runner->assetsByName) {
            s_obj_op = shget(runner->assetsByName, "oP");
        }
        if (s_obj_op < 0 || (uint32_t)s_obj_op >= runner->dataWin->objt.count) return;

        Instance** playerList = runner->instancesByObject[s_obj_op];
        if (!playerList || arrlen(playerList) == 0 || !playerList[0] || playerList[0]->destroyed) return;
        Instance* p = playerList[0];

        // Providence Arena is x >= 4150
        bool in_providence_arena = (p->x >= 4150.0f);

        if (in_providence_arena && !runner->sector_hibernation_active) {
            // Activate Sector Hibernation for Cargo Hold and non-bridge Decks (x < 4100)
            runner->sector_hibernation_active = true;
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
                    }
                }

                // Hibernate non-arena instance!
                inst->active = false;
                inst->sectorHibernated = true;
                hibernatedCount++;
            }

            logInfo("NCLP: Activated Sector Hibernation for r6_1_1 Providence Arena (%d non-arena instances hibernated).\n", hibernatedCount);
        } else if (!in_providence_arena && runner->sector_hibernation_active) {
            // Player retreated or exited arena: wake up hibernated instances
            runner->sector_hibernation_active = false;
            int32_t instCount = (int32_t)arrlen(runner->instances);
            int32_t wokeCount = 0;

            for (int32_t i = 0; i < instCount; i++) {
                Instance* inst = runner->instances[i];
                if (inst && inst->sectorHibernated) {
                    inst->active = true;
                    inst->sectorHibernated = false;
                    wokeCount++;
                }
            }
            logInfo("NCLP: Deactivated Sector Hibernation for r6_1_1 (%d instances resumed).\n", wokeCount);
        }
    }
}

bool NCLP_overrideEvent(Runner* runner, Instance* inst, int32_t eventType, int32_t eventSubtype, int32_t codeId, int32_t ownerObjectIndex) {
    (void)codeId;
    (void)ownerObjectIndex;
    if (!runner || !inst) return false;

    // 1. Gilded Wurm Body Segment Kinematics (oWurmBody End Step)
    if (s_obj_wurm_body >= 0 && inst->objectIndex == s_obj_wurm_body) {
        if (eventType == EVENT_STEP && eventSubtype == STEP_END) {
            if (s_var_parent_id < 0 && runner->vmContext) {
                s_var_parent_id = VM_getOrAllocateVarID(runner->vmContext, "parent");
            }
            if (s_var_parent_id >= 0) {
                RValue* slot = IntRValueHashMap_findSlot(&inst->selfVars, s_var_parent_id);
                if (slot) {
                    int32_t parentId = RValue_toInt32(*slot);
                    if (parentId >= 0 && runner->instancesById) {
                        Instance* parentInst = hmget(runner->instancesById, parentId);
                        if (parentInst && !parentInst->destroyed) {
                            float dx = parentInst->x - inst->x;
                            float dy = parentInst->y - inst->y;
                            float dist = sqrtf(dx * dx + dy * dy);

                            if (dist > 1.0f) {
                                float targetDist = 16.0f; // Segment spacing
                                inst->x = parentInst->x - dx * (targetDist / dist);
                                inst->y = parentInst->y - dy * (targetDist / dist);
                                inst->imageAngle = (GMLReal)(atan2f(-dy, dx) * (180.0f / 3.14159265f));
                                SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
                            }
                        }
                    }
                }
            }
            return true; // Completely bypassed GML bytecode VM execution!
        }
    }

    // 2. Temple Guardian Projectiles (oGuardBullet End Step Motion)
    if (s_obj_guard_bullet >= 0 && inst->objectIndex == s_obj_guard_bullet) {
        if (eventType == EVENT_STEP && eventSubtype == STEP_END) {
            inst->x += inst->hspeed;
            inst->y += inst->vspeed;
            SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
            return true; // Completely bypassed GML bytecode VM execution!
        }
    }

    return false;
}
