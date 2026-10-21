#!/usr/bin/env python3
"""
tests/test_clean_release_dist.py
Comprehensive verification test for Clean-Room / BYOD GitHub Release Distribution.
Validates:
1. Absolute absence of copyrighted/proprietary assets (*.win, *.ogg, Save.ini, .exe, logs).
2. Presence of required engine binaries, autonomous setup tool, scripts, libraries, shortcuts.
3. POSIX executable bits (0o755) in ZIP metadata for Linux execution.
4. Total package size ceiling (< 5 MB).
"""

import os
import sys
import zipfile
import unittest

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ZIP_PATH = os.path.join(PROJECT_ROOT, "dist", "Risk_of_Rain_MiyooMiniPlus.zip")
LAUNCH_SH_PATH = os.path.join(PROJECT_ROOT, "dist", "Roms", "PORTS", "Games", "Risk of Rain", "launch.sh")

PROHIBITED_EXTENSIONS = {".win", ".ogg", ".exe", ".log", ".bak", ".tmp"}
PROHIBITED_NAMES = {"save.ini", ".installed", "thumbs.db", ".ds_store"}

REQUIRED_RELEASE_FILES = {
    "Roms/PORTS/Games/Risk of Rain/RiskOfRain",
    "Roms/PORTS/Games/Risk of Rain/setup_miyoo",
    "Roms/PORTS/Games/Risk of Rain/launch.sh",
    "Roms/PORTS/Games/Risk of Rain/libs/libSDL-1.2.so.0",
    "Roms/PORTS/Games/Risk of Rain/properties/control.json",
    "Roms/PORTS/Games/Risk of Rain/PUT_PC_FILES_HERE.txt",
    "Roms/PORTS/Shortcuts/Action/Risk of Rain.port",
    "Roms/PORTS/Imgs/Risk of Rain.png"
}

REQUIRED_EXECUTABLES = {
    "Roms/PORTS/Games/Risk of Rain/RiskOfRain",
    "Roms/PORTS/Games/Risk of Rain/setup_miyoo",
    "Roms/PORTS/Games/Risk of Rain/launch.sh",
    "Roms/PORTS/Shortcuts/Action/Risk of Rain.port"
}

class TestCleanReleaseDist(unittest.TestCase):
    def test_01_zip_exists_and_compact(self):
        self.assertTrue(os.path.isfile(ZIP_PATH), f"Release zip missing at {ZIP_PATH}")
        size_bytes = os.path.getsize(ZIP_PATH)
        size_mb = size_bytes / (1024 * 1024)
        print(f"\n[TEST] Archive Size: {size_mb:.2f} MB ({size_bytes:,} bytes)")
        self.assertLess(size_mb, 5.0, f"Zip archive is {size_mb:.2f} MB, exceeds 5 MB clean release budget!")

    def test_02_zero_copyrighted_assets(self):
        with zipfile.ZipFile(ZIP_PATH, "r") as z:
            for info in z.infolist():
                fname = os.path.basename(info.filename)
                fname_lower = fname.lower()
                ext = os.path.splitext(fname_lower)[1]
                
                self.assertNotIn(ext, PROHIBITED_EXTENSIONS, 
                                 f"PROHIBITED EXTENSION FOUND: {info.filename} violates clean-room distribution!")
                self.assertNotIn(fname_lower, PROHIBITED_NAMES, 
                                 f"PROHIBITED FILE FOUND: {info.filename} violates clean-room distribution!")

    def test_03_required_files_present(self):
        with zipfile.ZipFile(ZIP_PATH, "r") as z:
            names = set(z.namelist())
            for req in REQUIRED_RELEASE_FILES:
                self.assertIn(req, names, f"Required file '{req}' missing from release package!")

    def test_04_executable_permissions(self):
        with zipfile.ZipFile(ZIP_PATH, "r") as z:
            for info in z.infolist():
                if info.filename in REQUIRED_EXECUTABLES:
                    perms = (info.external_attr >> 16) & 0o777
                    self.assertEqual(perms, 0o755, 
                                     f"File '{info.filename}' lacks 0o755 executable permission (got {oct(perms)})")

    def test_05_launch_script_hooks_setup(self):
        self.assertTrue(os.path.isfile(LAUNCH_SH_PATH), "launch.sh not found")
        with open(LAUNCH_SH_PATH, "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn("setup_miyoo", content, "launch.sh does not call or mention setup_miyoo!")
        self.assertIn(".installed", content, "launch.sh does not check .installed transaction marker!")

if __name__ == "__main__":
    unittest.main()
