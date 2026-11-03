import os
import unittest
import struct

class TestGameplaySaveState(unittest.TestCase):
    def setUp(self):
        self.workspace_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.savestate_h_path = os.path.join(self.workspace_dir, "butterscotch", "src", "savestate.h")
        self.savestate_c_path = os.path.join(self.workspace_dir, "butterscotch", "src", "savestate.c")
        self.runner_c_path = os.path.join(self.workspace_dir, "butterscotch", "src", "runner.c")
        self.miyoo_c_path = os.path.join(self.workspace_dir, "butterscotch", "src", "backends", "miyoo.c")

        with open(self.savestate_h_path, "r", encoding="utf-8", errors="ignore") as f:
            self.savestate_h = f.read()
        with open(self.savestate_c_path, "r", encoding="utf-8", errors="ignore") as f:
            self.savestate_c = f.read()
        with open(self.runner_c_path, "r", encoding="utf-8", errors="ignore") as f:
            self.runner_c = f.read()
        with open(self.miyoo_c_path, "r", encoding="utf-8", errors="ignore") as f:
            self.miyoo_c = f.read()

    def test_header_and_constants(self):
        """Verifies magic bytes, version number, filename and 32-byte header struct."""
        self.assertIn("0x53544154", self.savestate_h) # "STAT"
        self.assertIn("#define ROR_SAVE_STATE_VERSION 2", self.savestate_h)
        self.assertIn('"autosave.state"', self.savestate_h)
        self.assertIn('"autosave.state.tmp"', self.savestate_h)
        self.assertIn("typedef struct {", self.savestate_h)
        self.assertIn("uint32_t magic;", self.savestate_h)
        self.assertIn("uint32_t payloadSize;", self.savestate_h)
        self.assertIn("uint32_t crc32;", self.savestate_h)
        self.assertIn("int32_t  roomIndex;", self.savestate_h)

    def test_anti_corrupt_two_phase_atomic_write(self):
        """SaveState_save must write to temporary file, fsync, and rename atomically."""
        self.assertIn('fopen(ROR_SAVE_STATE_TMP_FILENAME, "wb")', self.savestate_c)
        self.assertIn("fsync(fileno(f))", self.savestate_c)
        self.assertIn("rename(ROR_SAVE_STATE_TMP_FILENAME, ROR_SAVE_STATE_FILENAME)", self.savestate_c)
        self.assertIn("crc32_calculate(bb.data, bb.size)", self.savestate_c)

    def test_anti_corrupt_verification_and_rejection(self):
        """SaveState_verify must validate magic, version, exact payload length, and CRC32."""
        self.assertIn("SaveState_verify", self.savestate_c)
        self.assertIn("header.magic != ROR_SAVE_STATE_MAGIC", self.savestate_c)
        self.assertIn("header.version != ROR_SAVE_STATE_VERSION", self.savestate_c)
        self.assertIn("calcCrc == header.crc32", self.savestate_c)

    def test_state_consumption_death_loop_prevention(self):
        """State must be deleted/consumed upon successful restoration to prevent death loops."""
        self.assertIn("SaveState_delete()", self.savestate_c)
        restore_idx = self.savestate_c.find("bool SaveState_restore(Runner* runner)")
        self.assertNotEqual(restore_idx, -1)
        restore_body = self.savestate_c[restore_idx:]
        self.assertIn("SaveState_delete();", restore_body)

    def test_save_state_can_save_conditions(self):
        """SaveState_canSave must gate saving strictly to active gameplay with living player."""
        self.assertIn("isGameplayRoom(runner->currentRoom->name)", self.savestate_c)
        self.assertIn('"oP"', self.savestate_c)
        self.assertIn('"oEfPlayerDead"', self.savestate_c)
        self.assertIn('"oGameOver"', self.savestate_c)
        self.assertIn('"dead"', self.savestate_c)

    def test_var_id_preservation_and_backgrounds(self):
        """VarIDs, dynamic var maps, and backgrounds must be serialized and restored."""
        self.assertIn("ctx->nextDynamicVarID", self.savestate_c)
        self.assertIn("ctx->varNameMap", self.savestate_c)
        self.assertIn("runner->backgrounds", self.savestate_c)
        self.assertIn("runner->backgroundColor", self.savestate_c)
        self.assertIn("runner->drawBackgroundColor", self.savestate_c)

    def test_ds_pools_and_instance_serialization(self):
        """ds_list, ds_map, ds_grid pools and active instances must be serialized."""
        self.assertIn("runner->dsListPool", self.savestate_c)
        self.assertIn("runner->dsMapPool", self.savestate_c)
        self.assertIn("runner->dsGridPool", self.savestate_c)
        self.assertIn("Instance_create", self.savestate_c)
        self.assertIn("Runner_addInstanceToObjectLists", self.savestate_c)
        self.assertIn("SpatialGrid_syncGrid", self.savestate_c)

    def test_runner_boot_resume_hook(self):
        """runner.c must intercept rInit transition to stop stray boot audio and auto-resume without firing OTHER_ROOM_START."""
        self.assertIn('#include "savestate.h"', self.runner_c)
        self.assertIn("SaveState_exists()", self.runner_c)
        self.assertIn("runner->audioSystem->vtable->stopAll(runner->audioSystem);", self.runner_c)
        self.assertIn("SaveState_restore(runner)", self.runner_c)
        # Ensure OTHER_ROOM_START is not triggered on restore
        restore_pos = self.runner_c.find("SaveState_restore(runner)")
        self.assertNotEqual(restore_pos, -1)
        block = self.runner_c[restore_pos:restore_pos + 400]
        self.assertNotIn("OTHER_ROOM_START", block)

    def test_audio_state_serialization_and_stray_sound_kill(self):
        """Audio group gains, looping/music sounds and positions must be preserved, and stray sounds killed."""
        # Check stray sounds killed on restore
        self.assertIn("runner->audioSystem->vtable->stopAll(runner->audioSystem);", self.savestate_c)

        # Check group gain serialization and restoration
        self.assertIn("runner->audioSystem->groupGainCount", self.savestate_c)
        self.assertIn("AudioSystem_setGroupGain", self.savestate_c)

        # Check looping / music sound serialization
        self.assertIn("ma_sound_is_looping(&inst->maSound)", self.savestate_c)
        self.assertIn("bb_write_i32(&bb, savedSounds[s].soundIndex);", self.savestate_c)
        self.assertIn("bb_write_i32(&bb, savedSounds[s].instanceId);", self.savestate_c)
        self.assertIn("bb_write_f32(&bb, savedSounds[s].trackPosition);", self.savestate_c)

        # Check restoration with ID remapping and seek position
        self.assertIn("runner->audioSystem->vtable->playSound(runner->audioSystem, soundIndex, priority, isLooping != 0);", self.savestate_c)
        self.assertIn("ma->instances[instIdx].instanceId = savedInstanceId;", self.savestate_c)
        self.assertIn("runner->audioSystem->vtable->setTrackPosition(runner->audioSystem, savedInstanceId, trackPosition);", self.savestate_c)
        self.assertIn("runner->audioSystem->vtable->setSoundGain(runner->audioSystem, savedInstanceId, currentGain, 0);", self.savestate_c)
        self.assertIn("runner->audioSystem->vtable->setSoundPitch(runner->audioSystem, savedInstanceId, pitch);", self.savestate_c)

    def test_runner_room_change_cleanup_hook(self):
        """runner.c must delete saved state when transitioning from gameplay to menu."""
        self.assertIn("isGameplayRoom(oldRoomName) && !isGameplayRoom(newRoomName)", self.runner_c)
        self.assertIn("SaveState_delete();", self.runner_c)

    def test_miyoo_backend_exit_and_signal_hooks(self):
        """miyoo.c must intercept MENU button and SIGTERM/SIGINT to auto-save or delete state."""
        self.assertIn('#include "savestate.h"', self.miyoo_c)
        self.assertIn("miyoo_exit_signal_handler", self.miyoo_c)
        self.assertIn("signal(SIGTERM, miyoo_exit_signal_handler)", self.miyoo_c)
        self.assertIn("signal(SIGINT, miyoo_exit_signal_handler)", self.miyoo_c)
        self.assertIn("SaveState_canSave(g_runner)", self.miyoo_c)
        self.assertIn("SaveState_save(g_runner)", self.miyoo_c)

    def test_string_deserialization_ownership_and_safety(self):
        """Strings (level_name, enemy state machine) must be deserialized as owned strings without dangling pointers."""
        self.assertIn("RValue_makeOwnedString", self.savestate_c)
        idx_deser = self.savestate_c.find("deserialize_rvalue")
        self.assertNotEqual(idx_deser, -1)
        idx = self.savestate_c.find("case RVALUE_STRING:", idx_deser)
        self.assertNotEqual(idx, -1)
        str_block = self.savestate_c[idx:idx + 250]
        self.assertIn("return RValue_makeOwnedString(s);", str_block)

    def test_instance_room_index_and_alarm_sync(self):
        """Restored instances must have roomIndex set and activeAlarmMask synchronized."""
        self.assertIn("inst->roomIndex = targetRoom;", self.savestate_c)
        self.assertIn("inst->activeAlarmMask |= (uint16_t)(1u << a);", self.savestate_c)
        self.assertIn("header.stepCount = runner->nextInstanceId;", self.savestate_c)

    def test_direct_var_id_preservation(self):
        """Hashmaps must directly store and restore int32_t keys to preserve compiled varIDs (team, hp, target)."""
        idx_ser = self.savestate_c.find("static void serialize_hashmap")
        self.assertNotEqual(idx_ser, -1)
        ser_block = self.savestate_c[idx_ser:idx_ser + 700]
        self.assertIn("bb_write_i32(bb, map->entries[i].key);", ser_block)

        idx_deser = self.savestate_c.find("static void deserialize_hashmap")
        self.assertNotEqual(idx_deser, -1)
        deser_block = self.savestate_c[idx_deser:idx_deser + 700]
        self.assertIn("int32_t key = bb_read_i32(bb);", deser_block)
        self.assertIn("IntRValueHashMap_getOrInsertUndefined(map, key);", deser_block)

if __name__ == "__main__":
    unittest.main()
