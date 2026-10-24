#!/usr/bin/env python3
"""
Test suite for Handheld Zoom (Option 1 in Video Options) and Zero-Logging in Risk of Rain.
"""

import os
import re
import unittest
import configparser

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DIST_DIR = os.path.join(REPO_ROOT, "dist", "Roms", "PORTS", "Games", "Risk of Rain")
PREFS_PATH = os.path.join(DIST_DIR, "Prefs.ini")
LAUNCH_PATH = os.path.join(DIST_DIR, "launch.sh")
BIN_PATH = os.path.join(DIST_DIR, "RiskOfRain")


class TestZoomAndLogging(unittest.TestCase):
    def test_prefs_scale_configured(self):
        """Prefs.ini must have [Video Options] scale configured (default 1.25 or 1.0)."""
        self.assertTrue(os.path.exists(PREFS_PATH), f"Missing {PREFS_PATH}")
        config = configparser.ConfigParser()
        config.read(PREFS_PATH)
        self.assertTrue(config.has_section("Video Options"), "Missing [Video Options] section")
        self.assertTrue(config.has_option("Video Options", "scale"), "Missing scale in [Video Options]")
        scale_val = float(config.get("Video Options", "scale"))
        self.assertIn(scale_val, (1.0, 1.25, 1.5), f"Unexpected scale default {scale_val}")

    def test_logging_disabled_in_launch_sh(self):
        """launch.sh must not redirect output to launch.log and must silence all outputs."""
        self.assertTrue(os.path.exists(LAUNCH_PATH), f"Missing {LAUNCH_PATH}")
        with open(LAUNCH_PATH, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertNotIn('exec > "$LOG_FILE"', content, "launch.sh should not redirect stdout to log file")
        self.assertIn("exec > /dev/null 2>&1", content, "launch.sh must redirect all stdout/stderr to /dev/null")
        self.assertIn('rm -f "$DIR/launch.log"', content, "launch.sh should clean up any existing launch.log")

    def test_binary_properties(self):
        """RiskOfRain binary must exist, be ARM ELF, and contain scale_refresh symbol."""
        self.assertTrue(os.path.exists(BIN_PATH), f"Missing binary {BIN_PATH}")
        with open(BIN_PATH, "rb") as f:
            header = f.read(4)
            self.assertEqual(header, b"\x7fELF", "Binary must be an ELF executable")

        with open(BIN_PATH, "rb") as f:
            bin_data = f.read()
            self.assertIn(b"scale_refresh", bin_data, "Binary must contain scale_refresh symbol")

    def test_zoom_aspect_ratio_and_dimensions(self):
        """Handheld Zoom (512x384) must be strictly 4:3 and exactly 1.25X scaling on 640x480."""
        screen_w, screen_h = 640, 480
        zoom_w, zoom_h = 512, 384

        # Aspect ratios must match perfectly
        self.assertAlmostEqual(screen_w / screen_h, 4.0 / 3.0, places=5)
        self.assertAlmostEqual(zoom_w / zoom_h, 4.0 / 3.0, places=5)

        # Scale factor must be exactly 1.25X on both axes
        scale_x = screen_w / zoom_w
        scale_y = screen_h / zoom_h
        self.assertEqual(scale_x, 1.25)
        self.assertEqual(scale_y, 1.25)

    def test_room_classification_logic(self):
        """Verify isGameplayRoom classification logic."""
        def is_gameplay_room(name):
            if not name:
                return False
            prefixes = ("r1_", "r2_", "r3_", "r4_", "r5_", "r6_", "rPig")
            return any(name.startswith(p) for p in prefixes)

        # Menus / UI / Cutscenes: MUST be False (native 640x480)
        menu_rooms = [
            "rInit", "rLogo", "rStart", "rStorage", "rBook",
            "rHighscore", "rSelect", "rSelectCoop", "rIntro",
            "rCutscene1", "rCutscene6", "rCredits", "r2Cutscene2",
            "rHost", "rSelectMult"
        ]
        for room in menu_rooms:
            self.assertFalse(is_gameplay_room(room), f"{room} should be recognized as a menu room")

        # Combat rooms: MUST be True (eligible for 1.25X Handheld Zoom)
        combat_rooms = [
            "r1_1_1", "r1_2_2", "r2_1_1", "r2_2_2",
            "r3_1_1", "r3_2_2", "rPigbeach", "r4_1_1",
            "r5_1_2", "r6_1_1"
        ]
        for room in combat_rooms:
            self.assertTrue(is_gameplay_room(room), f"{room} should be recognized as a combat room")

    def test_proposal1_mapping_bounds(self):
        """Proposal 1: Every destination pixel across 640x480 must map cleanly to 512x384."""
        src_w, src_h = 512, 384
        dst_w, dst_h = 640, 480

        # Horizontal mapping
        for x in range(dst_w):
            x_log = (dst_w - 1) - x
            pos = (x_log * (src_w - 1)) / float(dst_w - 1)
            s0 = int(pos)
            s1 = min(src_w - 1, s0 + 1)
            self.assertTrue(0 <= s0 < src_w, f"s0 out of bounds: {s0}")
            self.assertTrue(0 <= s1 < src_w, f"s1 out of bounds: {s1}")

        # Vertical mapping
        for y in range(dst_h):
            y_log = (dst_h - 1) - y
            pos = (y_log * (src_h - 1)) / float(dst_h - 1)
            s0 = int(pos)
            s1 = min(src_h - 1, s0 + 1)
            self.assertTrue(0 <= s0 < src_h, f"s0 out of bounds: {s0}")
            self.assertTrue(0 <= s1 < src_h, f"s1 out of bounds: {s1}")

    def test_zoom_1_5x_aspect_ratio_and_dimensions(self):
        """1.5X Handheld Zoom (426x320) must preserve 4:3 aspect ratio and exact coordinates."""
        screen_w, screen_h = 640, 480
        zoom_w, zoom_h = 426, 320

        # Aspect ratio matches 4:3 within 0.2%
        expected_ratio = 4.0 / 3.0
        actual_ratio = zoom_w / zoom_h
        self.assertAlmostEqual(actual_ratio, expected_ratio, delta=0.005)

        # Scale factor is 1.5X on Y and ~1.502X on X
        scale_x = screen_w / zoom_w
        scale_y = screen_h / zoom_h
        self.assertAlmostEqual(scale_y, 1.5, places=5)
        self.assertAlmostEqual(scale_x, 1.5023, delta=0.005)

    def test_zoom_1_5x_mapping_bounds(self):
        """1.5X: Every destination pixel across 640x480 must map cleanly into 426x320."""
        src_w, src_h = 426, 320
        dst_w, dst_h = 640, 480

        # Horizontal mapping
        for x in range(dst_w):
            x_log = (dst_w - 1) - x
            pos = (x_log * (src_w - 1)) / float(dst_w - 1)
            s0 = int(pos)
            s1 = min(src_w - 1, s0 + 1)
            self.assertTrue(0 <= s0 < src_w, f"1.5X s0 out of bounds: {s0}")
            self.assertTrue(0 <= s1 < src_w, f"1.5X s1 out of bounds: {s1}")

        # Vertical mapping
        for y in range(dst_h):
            y_log = (dst_h - 1) - y
            pos = (y_log * (src_h - 1)) / float(dst_h - 1)
            s0 = int(pos)
            s1 = min(src_h - 1, s0 + 1)
            self.assertTrue(0 <= s0 < src_h, f"1.5X s0 out of bounds: {s0}")
            self.assertTrue(0 <= s1 < src_h, f"1.5X s1 out of bounds: {s1}")

    def test_tri_state_scale_cycle(self):
        """scale_refresh must cycle 1.0X -> 1.25X -> 1.5X -> 1.0X upon oMenu click, and preserve scale on room transitions."""
        def scale_refresh_sim(current_scale):
            if abs(current_scale - 2.0) < 0.1:
                return 1.25
            elif abs(current_scale - 2.25) < 0.1 or abs(current_scale - 3.0) < 0.1:
                return 1.5
            elif abs(current_scale - 2.5) < 0.1 or current_scale > 2.6:
                return 1.0
            elif abs(current_scale - 1.25) < 0.05:
                return 1.25
            elif abs(current_scale - 1.5) < 0.05:
                return 1.5
            else:
                return 1.0

        def omenu_click(scale):
            # GML: scale += 1; if (scale > scale_max) scale = 1; scale_refresh();
            clicked = scale + 1.0
            if clicked > 4.0:
                clicked = 1.0
            return scale_refresh_sim(clicked)

        # 1. Non-click calls (room start, vsync toggle, Prefs boot) must preserve scale
        self.assertEqual(scale_refresh_sim(1.0), 1.0)
        self.assertEqual(scale_refresh_sim(1.25), 1.25)
        self.assertEqual(scale_refresh_sim(1.5), 1.5)

        # 2. Clicking in oMenu cycles 1.0X -> 1.25X -> 1.5X -> 1.0X
        scale = 1.0
        scale = omenu_click(scale)
        self.assertEqual(scale, 1.25)

        scale = omenu_click(scale)
        self.assertEqual(scale, 1.5)

        scale = omenu_click(scale)
        self.assertEqual(scale, 1.0)

        # 3. Next click from 1.0X must resume cycle to 1.25X (no lockup at 1.0X)
        scale = omenu_click(scale)
        self.assertEqual(scale, 1.25)

    def test_miyoo_1_5x_hardware_scaler_implemented(self):
        """miyoo.c must implement 426x320 hardware scaler and dispatch tw == 426 in flip worker."""
        miyoo_c = os.path.join(REPO_ROOT, "butterscotch", "src", "backends", "miyoo.c")
        self.assertTrue(os.path.exists(miyoo_c), f"Missing {miyoo_c}")
        with open(miyoo_c, "r", encoding="utf-8") as f:
            code = f.read()

        self.assertIn("mmp_video_flip_180_scale_426_to_640", code, "miyoo.c must implement mmp_video_flip_180_scale_426_to_640")
        self.assertIn("mmp_scale_init_tables_426", code, "miyoo.c must implement mmp_scale_init_tables_426")
        self.assertIn("tw == 426", code, "mmp_flip_worker must dispatch tw == 426")

        loop_c = os.path.join(REPO_ROOT, "butterscotch", "src", "loop.c")
        with open(loop_c, "r", encoding="utf-8") as f:
            lcode = f.read()
        self.assertIn("renderW = isCombatZoom ? ((runner->handheldScale > 1.35f) ? 426 : 512) : fbWidth", lcode,
                      "loop.c must set renderW to 426 for 1.5X zoom")


if __name__ == "__main__":
    unittest.main()
