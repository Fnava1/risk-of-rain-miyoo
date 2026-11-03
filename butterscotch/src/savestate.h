#ifndef _BS_SAVESTATE_H_
#define _BS_SAVESTATE_H_

#include "runner.h"
#include <stdbool.h>
#include <stdint.h>

#define ROR_SAVE_STATE_MAGIC 0x53544154 // "STAT"
#define ROR_SAVE_STATE_VERSION 2
#define ROR_SAVE_STATE_FILENAME "autosave.state"
#define ROR_SAVE_STATE_TMP_FILENAME "autosave.state.tmp"

// Header of the binary save state file (32 bytes)
typedef struct {
    uint32_t magic;          // 0x53544154
    uint32_t version;        // 2
    uint32_t payloadSize;    // Size in bytes of payload following this header
    uint32_t crc32;          // CRC32 of the payload
    int32_t  roomIndex;      // Index of the room to restore
    uint32_t stepCount;      // Runner stepCount
    int32_t  frameCount;     // Runner frameCount
    uint32_t reserved;       // Alignment / future expansion
} SaveStateHeader;

// Checks whether a valid, intact autosave file exists
bool SaveState_exists(void);

// Checks if current runner state meets all criteria for automatic saving:
// 1. Room is a gameplay room (r1_* through r6_*, rPigbeach)
// 2. Player (oP) exists and is alive (oEfPlayerDead absent, dead <= 0, oGameOver absent)
bool SaveState_canSave(Runner* runner);

// Atomically serializes runner gameplay state to autosave.state (.tmp -> fsync -> rename)
// Returns true on success, false on error.
bool SaveState_save(Runner* runner);

// Deletes any existing autosave.state and temporary files
void SaveState_delete(void);

// Verifies magic, version, file size, and CRC32 of autosave file
bool SaveState_verify(const char* filepath);

// Restores runner state from autosave.state:
// - Loads room payload without executing creation code or create events
// - Restores globals, DS pools, instances, and cameras
// - Syncs spatial grid and rebuilds drawables
// - Consumes (deletes) the autosave file upon success
// Returns true on success, false on error.
bool SaveState_restore(Runner* runner);

#endif // _BS_SAVESTATE_H_
