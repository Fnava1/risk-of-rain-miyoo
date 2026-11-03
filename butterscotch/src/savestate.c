#include "savestate.h"
#include "instance.h"
#include "int_rvalue_hashmap.h"
#include "gml_array.h"
#include "spatial_grid.h"
#include "vm.h"
#include "native_level_subsystem.h"
#include "physics/physics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(USE_MINIAUDIO) || defined(USE_SDL1_AUDIO)
#include "ma_audio_system.h"
#endif

#ifndef MAX_SOUND_INSTANCES
#define MAX_SOUND_INSTANCES 128
#endif
#ifndef SOUND_INSTANCE_ID_BASE
#define SOUND_INSTANCE_ID_BASE 100000
#endif

#ifdef _WIN32
#include <io.h>
#define unlink _unlink
#define fsync _commit
#else
#include <unistd.h>
#endif

// ==============================================================================
// 1. IEEE 802.3 CRC32 Calculation
// ==============================================================================
static uint32_t crc32_calculate(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1)));
        }
    }
    return ~crc;
}

// ==============================================================================
// 2. Dynamic Byte Buffer (ByteBuffer)
// ==============================================================================
typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
    size_t offset;
    bool error;
} ByteBuffer;

static void bb_init(ByteBuffer* bb, size_t initialCap) {
    bb->capacity = initialCap > 1024 ? initialCap : 1024;
    bb->data = (uint8_t*)safeMalloc(bb->capacity);
    bb->size = 0;
    bb->offset = 0;
    bb->error = false;
}

static void bb_init_read(ByteBuffer* bb, uint8_t* src, size_t size) {
    bb->data = src;
    bb->size = size;
    bb->capacity = size;
    bb->offset = 0;
    bb->error = false;
}

static void bb_free(ByteBuffer* bb, bool freeData) {
    if (freeData && bb->data) {
        free(bb->data);
    }
    bb->data = NULL;
    bb->size = 0;
    bb->capacity = 0;
    bb->offset = 0;
}

static void bb_reserve(ByteBuffer* bb, size_t extra) {
    if (bb->size + extra > bb->capacity) {
        size_t newCap = bb->capacity * 2;
        if (newCap < bb->size + extra) newCap = bb->size + extra + 1024;
        bb->data = (uint8_t*)safeRealloc(bb->data, newCap);
        bb->capacity = newCap;
    }
}

static void bb_write(ByteBuffer* bb, const void* src, size_t len) {
    if (!len) return;
    bb_reserve(bb, len);
    memcpy(bb->data + bb->size, src, len);
    bb->size += len;
}

static void bb_write_u8(ByteBuffer* bb, uint8_t v) { bb_write(bb, &v, 1); }
static void bb_write_u16(ByteBuffer* bb, uint16_t v) { bb_write(bb, &v, 2); }
static void bb_write_u32(ByteBuffer* bb, uint32_t v) { bb_write(bb, &v, 4); }
static void bb_write_i32(ByteBuffer* bb, int32_t v) { bb_write(bb, &v, 4); }
static void bb_write_f32(ByteBuffer* bb, float v) { bb_write(bb, &v, sizeof(float)); }
static void bb_write_f64(ByteBuffer* bb, double v) { bb_write(bb, &v, sizeof(double)); }

static void bb_write_str(ByteBuffer* bb, const char* str) {
    if (!str) {
        bb_write_u32(bb, 0);
        return;
    }
    uint32_t len = (uint32_t)strlen(str);
    bb_write_u32(bb, len);
    if (len > 0) bb_write(bb, str, len);
}

static void bb_read(ByteBuffer* bb, void* dst, size_t len) {
    if (bb->offset + len > bb->size) {
        bb->error = true;
        if (dst) memset(dst, 0, len);
        return;
    }
    if (dst) memcpy(dst, bb->data + bb->offset, len);
    bb->offset += len;
}

static uint8_t bb_read_u8(ByteBuffer* bb) {
    uint8_t v = 0;
    bb_read(bb, &v, 1);
    return v;
}

static uint16_t bb_read_u16(ByteBuffer* bb) {
    uint16_t v = 0;
    bb_read(bb, &v, 2);
    return v;
}

static uint32_t bb_read_u32(ByteBuffer* bb) {
    uint32_t v = 0;
    bb_read(bb, &v, 4);
    return v;
}

static int32_t bb_read_i32(ByteBuffer* bb) {
    int32_t v = 0;
    bb_read(bb, &v, 4);
    return v;
}

static float bb_read_f32(ByteBuffer* bb) {
    float v = 0.0f;
    bb_read(bb, &v, sizeof(float));
    return v;
}

static double bb_read_f64(ByteBuffer* bb) {
    double v = 0.0;
    bb_read(bb, &v, sizeof(double));
    return v;
}

static char* bb_read_str(ByteBuffer* bb) {
    uint32_t len = bb_read_u32(bb);
    if (bb->error || len == 0) return NULL;
    if (bb->offset + len > bb->size) {
        bb->error = true;
        return NULL;
    }
    char* str = (char*)safeMalloc(len + 1);
    bb_read(bb, str, len);
    str[len] = '\0';
    return str;
}

// ==============================================================================
// 3. RValue Serialization & Deserialization
// ==============================================================================
static void serialize_rvalue(ByteBuffer* bb, const RValue* val) {
    if (!val) {
        bb_write_u8(bb, RVALUE_UNDEFINED);
        return;
    }
    bb_write_u8(bb, val->type);
    switch (val->type) {
        case RVALUE_UNDEFINED:
            break;
        case RVALUE_BOOL:
            bb_write_u8(bb, val->int32 ? 1 : 0);
            break;
        case RVALUE_INT32:
            bb_write_i32(bb, val->int32);
            break;
        case RVALUE_REAL:
            bb_write_f64(bb, (double)val->real);
            break;
        case RVALUE_INT64:
#ifndef NO_RVALUE_INT64
            bb_write(bb, &val->int64, sizeof(int64_t));
#else
            bb_write_i32(bb, val->int32);
#endif
            break;
        case RVALUE_STRING:
            bb_write_str(bb, val->string);
            break;
        case RVALUE_ARRAY: {
            GMLArray* arr = val->array;
            if (!arr) {
                bb_write_u8(bb, 0);
                break;
            }
            bb_write_u8(bb, 1);
            bb_write_u8(bb, (uint8_t)arr->type);
            if (arr->type == GML_LEGACY_ARRAY) {
                bb_write_i32(bb, arr->legacy.rowCount);
                for (int32_t r = 0; r < arr->legacy.rowCount; r++) {
                    bb_write_i32(bb, arr->legacy.rows[r].length);
                    for (int32_t c = 0; c < arr->legacy.rows[r].length; c++) {
                        serialize_rvalue(bb, &arr->legacy.rows[r].data[c]);
                    }
                }
            } else {
                bb_write_i32(bb, arr->modern.length);
                for (int32_t i = 0; i < arr->modern.length; i++) {
                    serialize_rvalue(bb, &arr->modern.data[i]);
                }
            }
            break;
        }
        case RVALUE_ASSETREF:
            bb_write_i32(bb, val->assetIndex);
            break;
        default:
            // Non-serializable types stored as undefined
            break;
    }
}

static RValue deserialize_rvalue(ByteBuffer* bb, Runner* runner) {
    uint8_t type = bb_read_u8(bb);
    if (bb->error) return RValue_makeUndefined();

    switch (type) {
        case RVALUE_UNDEFINED:
            return RValue_makeUndefined();
        case RVALUE_BOOL:
            return RValue_makeBool(bb_read_u8(bb) != 0);
        case RVALUE_INT32:
            return RValue_makeInt32(bb_read_i32(bb));
        case RVALUE_REAL:
            return RValue_makeReal((GMLReal)bb_read_f64(bb));
        case RVALUE_INT64: {
            int64_t v = 0;
#ifndef NO_RVALUE_INT64
            bb_read(bb, &v, sizeof(int64_t));
            return RValue_makeInt64(v);
#else
            return RValue_makeInt32(bb_read_i32(bb));
#endif
        }
        case RVALUE_STRING: {
            char* s = bb_read_str(bb);
            if (!s) return RValue_makeOwnedString(safeStrdup(""));
            return RValue_makeOwnedString(s);
        }
        case RVALUE_ARRAY: {
            uint8_t exists = bb_read_u8(bb);
            if (!exists) return RValue_makeUndefined();
            uint8_t arrType = bb_read_u8(bb);
            GMLArray* arr = GMLArray_create(runner->dataWin, 0);
            if (arrType == GML_LEGACY_ARRAY) {
                int32_t rowCount = bb_read_i32(bb);
                for (int32_t r = 0; r < rowCount; r++) {
                    int32_t colLen = bb_read_i32(bb);
                    for (int32_t c = 0; c < colLen; c++) {
                        RValue cell = deserialize_rvalue(bb, runner);
                        int32_t flatIdx = r * GML_LEGACY_ARRAY_STRIDE + c;
                        GMLArray_growTo(arr, flatIdx + 1);
                        RValue* slot = GMLArray_slot(arr, flatIdx);
                        if (slot) *slot = cell;
                        else RValue_free(&cell);
                    }
                }
            } else {
                int32_t length = bb_read_i32(bb);
                GMLArray_growTo(arr, length);
                for (int32_t i = 0; i < length; i++) {
                    RValue cell = deserialize_rvalue(bb, runner);
                    RValue* slot = GMLArray_slot(arr, i);
                    if (slot) *slot = cell;
                    else RValue_free(&cell);
                }
            }
            return RValue_makeArray(arr);
        }
        case RVALUE_ASSETREF: {
            int32_t assetIdx = bb_read_i32(bb);
            RValue res = RValue_makeUndefined();
            res.type = RVALUE_ASSETREF;
            res.assetIndex = assetIdx;
            return res;
        }
        default:
            return RValue_makeUndefined();
    }
}

// ==============================================================================
// 4. IntRValueHashMap Serialization / Deserialization (Direct VarID preservation)
// ==============================================================================
static void serialize_hashmap(ByteBuffer* bb, const IntRValueHashMap* map) {
    if (!map || map->count == 0) {
        bb_write_u32(bb, 0);
        return;
    }

    uint32_t occupied = 0;
    for (uint32_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].key != INT_RVALUE_HASHMAP_EMPTY_KEY) occupied++;
    }

    bb_write_u32(bb, occupied);
    for (uint32_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].key != INT_RVALUE_HASHMAP_EMPTY_KEY) {
            bb_write_i32(bb, map->entries[i].key);
            serialize_rvalue(bb, &map->entries[i].value);
        }
    }
}

static void deserialize_hashmap(ByteBuffer* bb, Runner* runner, IntRValueHashMap* map) {
    uint32_t count = bb_read_u32(bb);
    for (uint32_t i = 0; i < count; i++) {
        int32_t key = bb_read_i32(bb);
        RValue val = deserialize_rvalue(bb, runner);
        RValue* slot = IntRValueHashMap_getOrInsertUndefined(map, key);
        if (slot) {
            RValue_free(slot);
            *slot = val;
        } else {
            RValue_free(&val);
        }
    }
}

// ==============================================================================
// 5. Verification and Existence
// ==============================================================================
bool SaveState_exists(void) {
    struct stat st;
    if (stat(ROR_SAVE_STATE_FILENAME, &st) == 0 && st.st_size >= (off_t)sizeof(SaveStateHeader)) {
        return SaveState_verify(ROR_SAVE_STATE_FILENAME);
    }
    return false;
}

void SaveState_delete(void) {
    unlink(ROR_SAVE_STATE_FILENAME);
    unlink(ROR_SAVE_STATE_TMP_FILENAME);
}

bool SaveState_verify(const char* filepath) {
    if (!filepath) return false;
    FILE* f = fopen(filepath, "rb");
    if (!f) return false;

    SaveStateHeader header;
    if (fread(&header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return false;
    }

    if (header.magic != ROR_SAVE_STATE_MAGIC || header.version != ROR_SAVE_STATE_VERSION) {
        fclose(f);
        return false;
    }

    if (header.payloadSize == 0 || header.payloadSize > 64 * 1024 * 1024) {
        fclose(f);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, sizeof(header), SEEK_SET);

    if (fileSize != (long)(sizeof(header) + header.payloadSize)) {
        fclose(f);
        return false;
    }

    uint8_t* payload = (uint8_t*)safeMalloc(header.payloadSize);
    if (fread(payload, 1, header.payloadSize, f) != header.payloadSize) {
        free(payload);
        fclose(f);
        return false;
    }
    fclose(f);

    uint32_t calcCrc = crc32_calculate(payload, header.payloadSize);
    free(payload);

    return (calcCrc == header.crc32);
}

bool SaveState_canSave(Runner* runner) {
    if (!runner || !runner->currentRoom || !runner->dataWin) return false;

    // 1. Must be a gameplay room
    if (!isGameplayRoom(runner->currentRoom->name)) return false;

    // 2. Player (oP) must exist and be active
    int32_t opIndex = shget(runner->assetsByName, "oP");
    if (opIndex < 0 || (uint32_t)opIndex >= runner->dataWin->objt.count) return false;
    Instance** playerList = runner->instancesByObject[opIndex];
    if (!playerList || arrlen(playerList) == 0 || !playerList[0] || playerList[0]->destroyed) {
        return false;
    }

    // 3. Player must not be dead
    int32_t deadObjIndex = shget(runner->assetsByName, "oEfPlayerDead");
    if (deadObjIndex >= 0 && (uint32_t)deadObjIndex < runner->dataWin->objt.count) {
        Instance** deadList = runner->instancesByObject[deadObjIndex];
        if (deadList && arrlen(deadList) > 0) return false;
    }

    int32_t gameOverIndex = shget(runner->assetsByName, "oGameOver");
    if (gameOverIndex >= 0 && (uint32_t)gameOverIndex < runner->dataWin->objt.count) {
        Instance** goList = runner->instancesByObject[gameOverIndex];
        if (goList && arrlen(goList) > 0) return false;
    }

    if (runner->vmContext) {
        int32_t deadVarId = VM_getOrAllocateVarID(runner->vmContext, "dead");
        if (deadVarId >= 0) {
            RValue* deadSlot = IntRValueHashMap_findSlot(&playerList[0]->selfVars, deadVarId);
            if (deadSlot && RValue_toReal(*deadSlot) > 0.0) return false;
        }
    }

    return true;
}

// ==============================================================================
// 6. Atomic Serialization (SaveState_save)
// ==============================================================================
bool SaveState_save(Runner* runner) {
    if (!SaveState_canSave(runner)) return false;

    VMContext* ctx = runner->vmContext;
    ByteBuffer bb;
    bb_init(&bb, 256 * 1024);

    // 1. Dynamic Variable Map (varNameMap & nextDynamicVarID)
    if (ctx) {
        bb_write_i32(&bb, ctx->nextDynamicVarID);
        int32_t mapLen = (int32_t)shlen(ctx->varNameMap);
        bb_write_i32(&bb, mapLen);
        for (int32_t i = 0; i < mapLen; i++) {
            bb_write_str(&bb, ctx->varNameMap[i].key);
            bb_write_i32(&bb, ctx->varNameMap[i].value);
        }
    } else {
        bb_write_i32(&bb, 0);
        bb_write_i32(&bb, 0);
    }

    // 2. Global Variables (exact VarID preservation)
    if (ctx && ctx->globalScopeInstance) {
        serialize_hashmap(&bb, &ctx->globalScopeInstance->selfVars);
    } else {
        bb_write_u32(&bb, 0);
    }

    // 3. Data Structures (ds_list, ds_map, ds_grid)
    // 3.1 ds_list pool
    int32_t listCount = (int32_t)arrlen(runner->dsListPool);
    bb_write_i32(&bb, listCount);
    for (int32_t i = 0; i < listCount; i++) {
        DsList* l = &runner->dsListPool[i];
        bb_write_u8(&bb, l->freed ? 1 : 0);
        if (!l->freed) {
            int32_t items = (int32_t)arrlen(l->items);
            bb_write_i32(&bb, items);
            for (int32_t j = 0; j < items; j++) {
                serialize_rvalue(&bb, &l->items[j]);
            }
        }
    }

    // 3.2 ds_map pool
    int32_t mapCount = (int32_t)arrlen(runner->dsMapPool);
    bb_write_i32(&bb, mapCount);
    for (int32_t i = 0; i < mapCount; i++) {
        DsMapEntry* m = runner->dsMapPool[i];
        if (!m) {
            bb_write_u8(&bb, 0);
        } else {
            bb_write_u8(&bb, 1);
            int32_t entryCount = (int32_t)shlen(m);
            bb_write_i32(&bb, entryCount);
            for (int32_t j = 0; j < entryCount; j++) {
                bb_write_str(&bb, m[j].key);
                serialize_rvalue(&bb, &m[j].value);
            }
        }
    }

    // 3.3 ds_grid pool
    int32_t gridCount = (int32_t)arrlen(runner->dsGridPool);
    bb_write_i32(&bb, gridCount);
    for (int32_t i = 0; i < gridCount; i++) {
        DsGrid* g = &runner->dsGridPool[i];
        bb_write_u8(&bb, g->freed ? 1 : 0);
        if (!g->freed) {
            bb_write_i32(&bb, g->width);
            bb_write_i32(&bb, g->height);
            int32_t total = g->width * g->height;
            for (int32_t j = 0; j < total; j++) {
                serialize_rvalue(&bb, &g->items[j]);
            }
        }
    }

    // 4. Active Instances
    uint32_t validInstances = 0;
    for (int32_t i = 0; i < (int32_t)arrlen(runner->instances); i++) {
        Instance* inst = runner->instances[i];
        if (inst && !inst->destroyed) validInstances++;
    }
    bb_write_u32(&bb, validInstances);

    for (int32_t i = 0; i < (int32_t)arrlen(runner->instances); i++) {
        Instance* inst = runner->instances[i];
        if (!inst || inst->destroyed) continue;

        bb_write_u32(&bb, inst->instanceId);
        bb_write_i32(&bb, inst->objectIndex);
        bb_write_f32(&bb, inst->x);
        bb_write_f32(&bb, inst->y);
        bb_write_f32(&bb, inst->xprevious);
        bb_write_f32(&bb, inst->yprevious);
        bb_write_f32(&bb, inst->xstart);
        bb_write_f32(&bb, inst->ystart);
        bb_write_u8(&bb, inst->persistent ? 1 : 0);
        bb_write_u8(&bb, inst->solid ? 1 : 0);
        bb_write_u8(&bb, inst->active ? 1 : 0);
        bb_write_u8(&bb, inst->visible ? 1 : 0);
        bb_write_i32(&bb, inst->maskIndex);

        bb_write_i32(&bb, inst->spriteIndex);
        bb_write_f32(&bb, inst->imageSpeed);
        bb_write_f32(&bb, inst->imageIndex);
        bb_write_f32(&bb, inst->imageXscale);
        bb_write_f32(&bb, inst->imageYscale);
        bb_write_f32(&bb, (float)inst->imageAngle);
        bb_write_f32(&bb, inst->imageAlpha);
        bb_write_u32(&bb, inst->imageBlend);
        bb_write_i32(&bb, inst->depth);
        bb_write_i32(&bb, inst->layer);

        bb_write_f32(&bb, inst->speed);
        bb_write_f32(&bb, inst->direction);
        bb_write_f32(&bb, inst->hspeed);
        bb_write_f32(&bb, inst->vspeed);
        bb_write_f32(&bb, inst->friction);
        bb_write_f32(&bb, inst->gravity);
        bb_write_f32(&bb, inst->gravityDirection);

        bb_write_u16(&bb, inst->activeAlarmMask);
        for (int a = 0; a < GML_ALARM_COUNT; a++) {
            bb_write_i32(&bb, inst->alarm[a]);
        }

        bb_write_i32(&bb, inst->pathIndex);
        bb_write_f32(&bb, inst->pathPosition);
        bb_write_f32(&bb, inst->pathPositionPrevious);
        bb_write_f32(&bb, inst->pathSpeed);
        bb_write_f32(&bb, inst->pathScale);
        bb_write_f32(&bb, inst->pathOrientation);
        bb_write_i32(&bb, inst->pathEndAction);
        bb_write_f32(&bb, inst->pathXStart);
        bb_write_f32(&bb, inst->pathYStart);

        bb_write_i32(&bb, inst->timelineIndex);
        bb_write_f32(&bb, inst->timelinePosition);
        bb_write_f32(&bb, inst->timelineSpeed);
        bb_write_u8(&bb, inst->timelineLoop ? 1 : 0);
        bb_write_u8(&bb, inst->timelineRunning ? 1 : 0);

        serialize_hashmap(&bb, &inst->selfVars);
    }

    // 5. Views and Camera state
    bb_write_f32(&bb, runner->handheldScale);
    bb_write_u8(&bb, runner->viewsEnabled ? 1 : 0);
    bb_write(&bb, runner->views, sizeof(runner->views));
    bb_write(&bb, runner->defaultCameras, sizeof(runner->defaultCameras));

    // 6. Backgrounds and Background Color
    bb_write_u32(&bb, runner->backgroundColor);
    bb_write_u8(&bb, runner->drawBackgroundColor ? 1 : 0);
    bb_write(&bb, runner->backgrounds, sizeof(runner->backgrounds));

    // 7. Audio State (Group gains and active looping/music sounds)
    uint32_t groupCount = 0;
    if (runner->audioSystem && runner->audioSystem->groupGains) {
        groupCount = runner->audioSystem->groupGainCount;
    }
    bb_write_u32(&bb, groupCount);
    for (uint32_t g = 0; g < groupCount; g++) {
        bb_write_f32(&bb, runner->audioSystem->groupGains[g].current);
    }

    typedef struct {
        int32_t soundIndex;
        int32_t instanceId;
        float currentGain;
        float targetGain;
        float pitch;
        float trackPosition;
        int32_t priority;
        uint8_t isLooping;
    } SavedSound;

    uint32_t soundCount = 0;
    SavedSound savedSounds[MAX_SOUND_INSTANCES];
#if defined(USE_MINIAUDIO) || defined(USE_SDL1_AUDIO)
    if (runner->audioSystem) {
        MaAudioSystem* ma = (MaAudioSystem*)runner->audioSystem;
        for (int32_t i = 0; i < MAX_SOUND_INSTANCES; i++) {
            SoundInstance* inst = &ma->instances[i];
            if (!inst->active) continue;

            bool isLooping = ma_sound_is_looping(&inst->maSound);
            bool isMusic = false;
            if (inst->soundIndex >= 0 && runner->dataWin && (uint32_t)inst->soundIndex < runner->dataWin->sond.count) {
                const char* sname = runner->dataWin->sond.sounds[inst->soundIndex].name;
                if (sname && (strncmp(sname, "music", 5) == 0 || strncmp(sname, "HitItSon", 8) == 0)) {
                    isMusic = true;
                }
            }

            if ((isLooping || isMusic) && ma_sound_is_playing(&inst->maSound)) {
                if (soundCount < MAX_SOUND_INSTANCES) {
                    SavedSound* ss = &savedSounds[soundCount++];
                    ss->soundIndex = inst->soundIndex;
                    ss->instanceId = inst->instanceId;
                    ss->currentGain = inst->currentGain;
                    ss->targetGain = inst->targetGain;
                    ss->pitch = ma_sound_get_pitch(&inst->maSound);
                    ss->trackPosition = (runner->audioSystem->vtable && runner->audioSystem->vtable->getTrackPosition) ?
                                        runner->audioSystem->vtable->getTrackPosition(runner->audioSystem, inst->instanceId) : 0.0f;
                    ss->priority = inst->priority;
                    ss->isLooping = (isLooping || isMusic) ? 1 : 0;
                }
            }
        }
    }
#endif
    bb_write_u32(&bb, soundCount);
    for (uint32_t s = 0; s < soundCount; s++) {
        bb_write_i32(&bb, savedSounds[s].soundIndex);
        bb_write_i32(&bb, savedSounds[s].instanceId);
        bb_write_f32(&bb, savedSounds[s].currentGain);
        bb_write_f32(&bb, savedSounds[s].targetGain);
        bb_write_f32(&bb, savedSounds[s].pitch);
        bb_write_f32(&bb, savedSounds[s].trackPosition);
        bb_write_i32(&bb, savedSounds[s].priority);
        bb_write_u8(&bb, savedSounds[s].isLooping);
    }

    // 8. Prepare Header
    SaveStateHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = ROR_SAVE_STATE_MAGIC;
    header.version = ROR_SAVE_STATE_VERSION;
    header.payloadSize = (uint32_t)bb.size;
    header.crc32 = crc32_calculate(bb.data, bb.size);
    header.roomIndex = runner->currentRoomIndex;
    header.stepCount = runner->nextInstanceId;
    header.frameCount = runner->frameCount;

    // 8. Atomic file write: write .tmp, fsync, close, rename
    FILE* f = fopen(ROR_SAVE_STATE_TMP_FILENAME, "wb");
    if (!f) {
        bb_free(&bb, true);
        return false;
    }

    bool writeSuccess = true;
    if (fwrite(&header, 1, sizeof(header), f) != sizeof(header)) writeSuccess = false;
    if (writeSuccess && fwrite(bb.data, 1, bb.size, f) != bb.size) writeSuccess = false;

    if (writeSuccess) {
        fflush(f);
        fsync(fileno(f));
    }
    fclose(f);
    bb_free(&bb, true);

    if (!writeSuccess) {
        unlink(ROR_SAVE_STATE_TMP_FILENAME);
        return false;
    }

    unlink(ROR_SAVE_STATE_FILENAME);
    if (rename(ROR_SAVE_STATE_TMP_FILENAME, ROR_SAVE_STATE_FILENAME) != 0) {
        unlink(ROR_SAVE_STATE_TMP_FILENAME);
        return false;
    }

    logInfo("SaveState: Successfully saved %u bytes (CRC32: 0x%08X) to %s\n",
            header.payloadSize, header.crc32, ROR_SAVE_STATE_FILENAME);
    return true;
}

// ==============================================================================
// 7. Deserialization & Recovery (SaveState_restore)
// ==============================================================================
bool SaveState_restore(Runner* runner) {
    if (!runner || !runner->dataWin) return false;
    if (!SaveState_verify(ROR_SAVE_STATE_FILENAME)) {
        SaveState_delete();
        return false;
    }

    FILE* f = fopen(ROR_SAVE_STATE_FILENAME, "rb");
    if (!f) return false;

    SaveStateHeader header;
    if (fread(&header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        SaveState_delete();
        return false;
    }

    uint8_t* payload = (uint8_t*)safeMalloc(header.payloadSize);
    if (fread(payload, 1, header.payloadSize, f) != header.payloadSize) {
        free(payload);
        fclose(f);
        SaveState_delete();
        return false;
    }
    fclose(f);

    ByteBuffer bb;
    bb_init_read(&bb, payload, header.payloadSize);

    // 1. Prepare target room
    int32_t targetRoom = header.roomIndex;
    if (targetRoom < 0 || (uint32_t)targetRoom >= runner->dataWin->room.count) {
        free(payload);
        SaveState_delete();
        return false;
    }

    Room* room = &runner->dataWin->room.rooms[targetRoom];
    if (!room->payloadLoaded) {
        DataWin_loadRoomPayload(runner->dataWin, targetRoom);
    }

    // 2. Clear current instances in runner
    for (int32_t i = 0; i < (int32_t)arrlen(runner->instances); i++) {
        Instance* inst = runner->instances[i];
        if (inst) {
            hmdel(runner->instancesById, inst->instanceId);
            Instance_free(inst);
        }
    }
    arrfree(runner->instances);
    runner->instances = NULL;

    for (uint32_t i = 0; i < runner->dataWin->objt.count; i++) {
        arrfree(runner->instancesByObject[i]);
        runner->instancesByObject[i] = NULL;
        arrfree(runner->instancesByExactObject[i]);
        runner->instancesByExactObject[i] = NULL;
    }

    // 3. Clear existing DS pools
    // ds_list pool
    for (int32_t i = 0; i < (int32_t)arrlen(runner->dsListPool); i++) {
        DsList* list = &runner->dsListPool[i];
        for (int32_t j = 0; j < (int32_t)arrlen(list->items); j++) {
            RValue_free(&list->items[j]);
        }
        arrfree(list->items);
    }
    arrfree(runner->dsListPool);
    runner->dsListPool = NULL;

    // ds_map pool
    for (int32_t i = 0; i < (int32_t)arrlen(runner->dsMapPool); i++) {
        DsMapEntry* m = runner->dsMapPool[i];
        if (m) {
            for (int32_t j = 0; j < (int32_t)shlen(m); j++) {
                free(m[j].key);
                RValue_free(&m[j].value);
            }
            shfree(m);
        }
    }
    arrfree(runner->dsMapPool);
    runner->dsMapPool = NULL;

    // ds_grid pool
    for (int32_t i = 0; i < (int32_t)arrlen(runner->dsGridPool); i++) {
        DsGrid* grid = &runner->dsGridPool[i];
        int32_t count = grid->width * grid->height;
        for (int32_t j = 0; j < count; j++) {
            RValue_free(&grid->items[j]);
        }
        free(grid->items);
    }
    arrfree(runner->dsGridPool);
    runner->dsGridPool = NULL;

    // Set active room
    runner->currentRoom = room;
    runner->currentRoomIndex = targetRoom;
    runner->currentRoomOrderPosition = -1;
    for (uint32_t i = 0; i < runner->dataWin->gen8.roomOrderCount; i++) {
        if (runner->dataWin->gen8.roomOrder[i] == targetRoom) {
            runner->currentRoomOrderPosition = (int32_t)i;
            break;
        }
    }

    Physics_initRoom(runner);
    if (runner->spatialGrid) SpatialGrid_free(runner->spatialGrid);
    runner->spatialGrid = SpatialGrid_create(room->width, room->height);

    // 4. Restore Dynamic Variables
    VMContext* ctx = runner->vmContext;
    int32_t nextDynId = bb_read_i32(&bb);
    int32_t dynMapLen = bb_read_i32(&bb);
    if (ctx && nextDynId > ctx->nextDynamicVarID) {
        ctx->nextDynamicVarID = nextDynId;
    }
    for (int32_t i = 0; i < dynMapLen; i++) {
        char* k = bb_read_str(&bb);
        int32_t v = bb_read_i32(&bb);
        if (k && ctx) {
            ptrdiff_t slot = shgeti(ctx->varNameMap, k);
            if (slot < 0) {
                shput(ctx->varNameMap, k, v);
            } else {
                ctx->varNameMap[slot].value = v;
                free(k);
            }
        } else if (k) {
            free(k);
        }
    }

    // 5. Restore Globals
    if (ctx && ctx->globalScopeInstance) {
        IntRValueHashMap_freeAllValues(&ctx->globalScopeInstance->selfVars);
        deserialize_hashmap(&bb, runner, &ctx->globalScopeInstance->selfVars);
    } else {
        uint32_t skipGlobals = bb_read_u32(&bb);
        (void)skipGlobals;
    }

    // 6. Restore Data Structures
    // 6.1 ds_list pool
    int32_t listPoolSize = bb_read_i32(&bb);
    for (int32_t i = 0; i < listPoolSize; i++) {
        DsList l;
        memset(&l, 0, sizeof(l));
        l.freed = (bb_read_u8(&bb) != 0);
        if (!l.freed) {
            int32_t itemCount = bb_read_i32(&bb);
            for (int32_t j = 0; j < itemCount; j++) {
                RValue v = deserialize_rvalue(&bb, runner);
                arrput(l.items, v);
            }
        }
        arrput(runner->dsListPool, l);
    }

    // 6.2 ds_map pool
    int32_t mapPoolSize = bb_read_i32(&bb);
    for (int32_t i = 0; i < mapPoolSize; i++) {
        uint8_t exists = bb_read_u8(&bb);
        if (!exists) {
            arrput(runner->dsMapPool, NULL);
        } else {
            DsMapEntry* m = NULL;
            int32_t entryCount = bb_read_i32(&bb);
            for (int32_t j = 0; j < entryCount; j++) {
                char* k = bb_read_str(&bb);
                RValue v = deserialize_rvalue(&bb, runner);
                if (k) {
                    shput(m, k, v);
                } else {
                    RValue_free(&v);
                }
            }
            arrput(runner->dsMapPool, m);
        }
    }

    // 6.3 ds_grid pool
    int32_t gridPoolSize = bb_read_i32(&bb);
    for (int32_t i = 0; i < gridPoolSize; i++) {
        DsGrid g;
        memset(&g, 0, sizeof(g));
        g.freed = (bb_read_u8(&bb) != 0);
        if (!g.freed) {
            g.width = bb_read_i32(&bb);
            g.height = bb_read_i32(&bb);
            int32_t total = g.width * g.height;
            g.items = total > 0 ? (RValue*)safeCalloc(total, sizeof(RValue)) : NULL;
            for (int32_t j = 0; j < total; j++) {
                g.items[j] = deserialize_rvalue(&bb, runner);
            }
        }
        arrput(runner->dsGridPool, g);
    }

    // 7. Restore Instances
    uint32_t instCount = bb_read_u32(&bb);
    for (uint32_t i = 0; i < instCount; i++) {
        uint32_t iId = bb_read_u32(&bb);
        int32_t oIdx = bb_read_i32(&bb);
        float x = bb_read_f32(&bb);
        float y = bb_read_f32(&bb);

        Instance* inst = Instance_create(iId, oIdx, (GMLReal)x, (GMLReal)y);
        inst->roomIndex = targetRoom;
        inst->xprevious = bb_read_f32(&bb);
        inst->yprevious = bb_read_f32(&bb);
        inst->xstart = bb_read_f32(&bb);
        inst->ystart = bb_read_f32(&bb);
        inst->persistent = (bb_read_u8(&bb) != 0);
        inst->solid = (bb_read_u8(&bb) != 0);
        inst->active = (bb_read_u8(&bb) != 0);
        inst->visible = (bb_read_u8(&bb) != 0);
        inst->maskIndex = bb_read_i32(&bb);

        inst->spriteIndex = bb_read_i32(&bb);
        inst->imageSpeed = bb_read_f32(&bb);
        inst->imageIndex = bb_read_f32(&bb);
        inst->imageXscale = bb_read_f32(&bb);
        inst->imageYscale = bb_read_f32(&bb);
        inst->imageAngle = (GMLReal)bb_read_f32(&bb);
        inst->imageAlpha = bb_read_f32(&bb);
        inst->imageBlend = bb_read_u32(&bb);
        inst->depth = bb_read_i32(&bb);
        inst->layer = bb_read_i32(&bb);

        inst->speed = bb_read_f32(&bb);
        inst->direction = bb_read_f32(&bb);
        inst->hspeed = bb_read_f32(&bb);
        inst->vspeed = bb_read_f32(&bb);
        inst->friction = bb_read_f32(&bb);
        inst->gravity = bb_read_f32(&bb);
        inst->gravityDirection = bb_read_f32(&bb);

        inst->activeAlarmMask = bb_read_u16(&bb);
        for (int a = 0; a < GML_ALARM_COUNT; a++) {
            inst->alarm[a] = bb_read_i32(&bb);
            if (inst->alarm[a] > 0) inst->activeAlarmMask |= (uint16_t)(1u << a);
            else inst->activeAlarmMask &= (uint16_t)~(1u << a);
        }

        inst->pathIndex = bb_read_i32(&bb);
        inst->pathPosition = bb_read_f32(&bb);
        inst->pathPositionPrevious = bb_read_f32(&bb);
        inst->pathSpeed = bb_read_f32(&bb);
        inst->pathScale = bb_read_f32(&bb);
        inst->pathOrientation = bb_read_f32(&bb);
        inst->pathEndAction = bb_read_i32(&bb);
        inst->pathXStart = bb_read_f32(&bb);
        inst->pathYStart = bb_read_f32(&bb);

        inst->timelineIndex = bb_read_i32(&bb);
        inst->timelinePosition = bb_read_f32(&bb);
        inst->timelineSpeed = bb_read_f32(&bb);
        inst->timelineLoop = (bb_read_u8(&bb) != 0);
        inst->timelineRunning = (bb_read_u8(&bb) != 0);

        deserialize_hashmap(&bb, runner, &inst->selfVars);

        arrput(runner->instances, inst);
        hmput(runner->instancesById, inst->instanceId, inst);
        Runner_addInstanceToObjectLists(runner, inst);

        if (inst->instanceId >= runner->nextInstanceId) {
            runner->nextInstanceId = inst->instanceId + 1;
        }
    }
    if (header.stepCount > runner->nextInstanceId) {
        runner->nextInstanceId = header.stepCount;
    }

    // 8. Restore Views and Cameras
    runner->handheldScale = bb_read_f32(&bb);
    runner->viewsEnabled = (bb_read_u8(&bb) != 0);
    bb_read(&bb, runner->views, sizeof(runner->views));
    bb_read(&bb, runner->defaultCameras, sizeof(runner->defaultCameras));

    if (isGameplayRoom(room->name) && runner->handheldScale > 1.1f) {
        int32_t targetW = (runner->handheldScale > 1.35f) ? 426 : 512;
        int32_t targetH = (runner->handheldScale > 1.35f) ? 320 : 384;
        runner->defaultCameras[0].viewWidth = targetW;
        runner->defaultCameras[0].viewHeight = targetH;
        Runner_updateCameraViewSimple(&runner->defaultCameras[0]);
        runner->views[0].portWidth = targetW;
        runner->views[0].portHeight = targetH;
    }

    // 9. Restore Backgrounds & Colors
    // First initialize from room definition
    runner->backgroundColor = room->backgroundColor;
    runner->drawBackgroundColor = room->drawBackgroundColor;
    for (int i = 0; i < 8; i++) {
        RoomBackground* src = &room->backgrounds[i];
        RuntimeBackground* dst = &runner->backgrounds[i];
        dst->visible = src->enabled;
        dst->foreground = src->foreground;
        dst->backgroundIndex = src->backgroundDefinition;
        dst->x = (float)src->x;
        dst->y = (float)src->y;
        dst->tileX = (bool)src->tileX;
        dst->tileY = (bool)src->tileY;
        dst->speedX = (float)src->speedX;
        dst->speedY = (float)src->speedY;
        dst->xScale = 1.0f;
        dst->yScale = 1.0f;
        dst->stretch = src->stretch;
        dst->alpha = 1.0f;
    }

    // Overwrite with saved runtime backgrounds and colors
    runner->backgroundColor = bb_read_u32(&bb);
    runner->drawBackgroundColor = (bb_read_u8(&bb) != 0);
    bb_read(&bb, runner->backgrounds, sizeof(runner->backgrounds));

    // 10. Stop stray sounds and Restore Audio State
    if (runner->audioSystem && runner->audioSystem->vtable && runner->audioSystem->vtable->stopAll) {
        runner->audioSystem->vtable->stopAll(runner->audioSystem);
    }

    uint32_t savedGroupCount = bb_read_u32(&bb);
    for (uint32_t g = 0; g < savedGroupCount; g++) {
        float gain = bb_read_f32(&bb);
        if (runner->audioSystem) {
            AudioSystem_setGroupGain(runner->audioSystem, (int32_t)g, gain, 0);
        }
    }

    uint32_t savedSoundCount = bb_read_u32(&bb);
    for (uint32_t s = 0; s < savedSoundCount; s++) {
        int32_t soundIndex = bb_read_i32(&bb);
        int32_t savedInstanceId = bb_read_i32(&bb);
        float currentGain = bb_read_f32(&bb);
        float targetGain = bb_read_f32(&bb);
        float pitch = bb_read_f32(&bb);
        float trackPosition = bb_read_f32(&bb);
        int32_t priority = bb_read_i32(&bb);
        uint8_t isLooping = bb_read_u8(&bb);

        if (runner->audioSystem && runner->audioSystem->vtable && runner->audioSystem->vtable->playSound) {
            int32_t newId = runner->audioSystem->vtable->playSound(runner->audioSystem, soundIndex, priority, isLooping != 0);
            if (newId >= 0) {
#if defined(USE_MINIAUDIO) || defined(USE_SDL1_AUDIO)
                MaAudioSystem* ma = (MaAudioSystem*)runner->audioSystem;
                for (int32_t instIdx = 0; instIdx < MAX_SOUND_INSTANCES; instIdx++) {
                    if (ma->instances[instIdx].active && ma->instances[instIdx].instanceId == newId) {
                        ma->instances[instIdx].instanceId = savedInstanceId;
                        ma->instances[instIdx].currentGain = currentGain;
                        ma->instances[instIdx].targetGain = targetGain;
                        break;
                    }
                }
                if (savedInstanceId >= SOUND_INSTANCE_ID_BASE) {
                    int32_t counter = (savedInstanceId - SOUND_INSTANCE_ID_BASE) + 1;
                    if (counter > ma->nextInstanceCounter) {
                        ma->nextInstanceCounter = counter;
                    }
                }
#endif
                runner->lastMusicInstance = savedInstanceId;
                if (runner->audioSystem->vtable->setTrackPosition && trackPosition > 0.0f) {
                    runner->audioSystem->vtable->setTrackPosition(runner->audioSystem, savedInstanceId, trackPosition);
                }
                if (runner->audioSystem->vtable->setSoundGain) {
                    runner->audioSystem->vtable->setSoundGain(runner->audioSystem, savedInstanceId, currentGain, 0);
                }
                if (runner->audioSystem->vtable->setSoundPitch && pitch > 0.0f) {
                    runner->audioSystem->vtable->setSoundPitch(runner->audioSystem, savedInstanceId, pitch);
                }
            }
        }
    }

    free(payload);

    // 10. Rebuild Drawables and Synchronize Spatial Grid
    runner->cachedRoomTilesRoomIndex = -1;
    runner->cachedRoomTilesSectorHibernated = false;
    runner->drawableListStructureDirty = true;
    SpatialGrid_syncGrid(runner, runner->spatialGrid);
    NCLP_onRoomStart(runner);

    runner->frameCount = header.frameCount;

    // 11. Consume autosave file to prevent death loops
    SaveState_delete();

    logInfo("SaveState: Restored room %s (%d) with %d instances (frame=%d)\n",
            room->name, targetRoom, (int)arrlen(runner->instances),
            runner->frameCount);
    return true;
}
