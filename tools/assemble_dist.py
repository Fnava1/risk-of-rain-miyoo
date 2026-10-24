#!/usr/bin/env python3
"""
tools/assemble_dist.py
Assembles and packages the OnionOS distribution for Risk of Rain (2013).
Creates the canonical Pure Ports Collection structure and generates a release ZIP.

STRICT BYOD (Bring Your Own Data) POLICY:
- Zero proprietary game assets (data.win, data_mmp.win, music*.ogg, Save.ini)
- Zero log files or temporary files
- Includes native port binary (RiskOfRain), on-device setup optimizer (setup_miyoo),
  launcher script (launch.sh), runtime libraries, configuration, and artwork.
"""

import os
import sys
import shutil
import zipfile

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DIST_DIR = os.path.join(PROJECT_ROOT, "dist")
ROMS_DIR = os.path.join(DIST_DIR, "Roms", "PORTS")
GAMES_DIR = os.path.join(ROMS_DIR, "Games", "Risk of Rain")
ZIP_OUTPUT = os.path.join(DIST_DIR, "Risk_of_Rain_MiyooMiniPlus.zip")

# Explicit blacklisted patterns / filenames to prevent packaging proprietary assets
PROPRIETARY_BLACKLIST = {
    "data.win", "data_mmp.win", "Save.ini", "launch.log", ".installed",
    "Risk of Rain.exe", "RiskofRain.exe"
}

def is_blacklisted(filename: str) -> bool:
    name_lower = filename.lower()
    if name_lower in {b.lower() for b in PROPRIETARY_BLACKLIST}:
        return True
    if name_lower.endswith(".ogg") or name_lower.endswith(".win") or name_lower.endswith(".exe"):
        return True
    if name_lower.endswith((".log", ".bak", ".tmp", "~")) or name_lower in {".ds_store", "thumbs.db"}:
        return True
    return False

def main():
    print("[*] Assembling OnionOS Clean Release Ports Distribution...")

    # Ensure required directories exist
    os.makedirs(os.path.join(ROMS_DIR, "Imgs"), exist_ok=True)
    os.makedirs(os.path.join(ROMS_DIR, "Shortcuts", "Action"), exist_ok=True)
    os.makedirs(os.path.join(GAMES_DIR, "properties"), exist_ok=True)
    os.makedirs(os.path.join(GAMES_DIR, "libs"), exist_ok=True)

    # 1. Verify RiskOfRain binary in distribution
    bin_dst = os.path.join(GAMES_DIR, "RiskOfRain")
    if not os.path.isfile(bin_dst):
        bin_src = os.path.join(PROJECT_ROOT, "butterscotch", "build_miyoo", "butterscotch")
        if os.path.isfile(bin_src):
            print(f"[*] Staging binary: {bin_src} -> {bin_dst}")
            shutil.copy2(bin_src, bin_dst)

    # Check setup_miyoo binary
    setup_bin = os.path.join(GAMES_DIR, "setup_miyoo")
    if not os.path.isfile(setup_bin):
        print("[!] ERROR: setup_miyoo binary not found! Please compile it first.")
        return 1

    if not os.path.isfile(bin_dst):
        print("[!] ERROR: RiskOfRain binary not found! Please build butterscotch first.")
        return 1

    # 2. Package into release ZIP
    print(f"[*] Creating clean release archive: {ZIP_OUTPUT}")
    if os.path.isfile(ZIP_OUTPUT):
        os.remove(ZIP_OUTPUT)

    packaged_files = []
    skipped_files = []

    with zipfile.ZipFile(ZIP_OUTPUT, "w", zipfile.ZIP_DEFLATED) as zipf:
        roms_base = os.path.join(DIST_DIR, "Roms")
        for root, dirs, files in os.walk(roms_base):
            for file in sorted(files):
                if is_blacklisted(file):
                    skipped_files.append(file)
                    continue

                file_path = os.path.join(root, file)
                rel_path = os.path.relpath(file_path, DIST_DIR).replace("\\", "/")

                # Preserve POSIX executable bits for ELF and scripts
                zip_info = zipfile.ZipInfo(rel_path)
                with open(file_path, "rb") as f:
                    content = f.read()

                if file.endswith((".sh", ".port")) or file in ("RiskOfRain", "setup_miyoo"):
                    zip_info.external_attr = 0o755 << 16  # rwxr-xr-x
                else:
                    zip_info.external_attr = 0o644 << 16  # rw-r--r--

                zipf.writestr(zip_info, content)
                packaged_files.append(rel_path)

    # 3. Audit check
    print(f"[*] Excluded {len(skipped_files)} proprietary/temporary files: {sorted(list(set(skipped_files)))}")
    print(f"[*] Included {len(packaged_files)} release files:")
    for pf in packaged_files:
        print(f"    - {pf}")

    # Double check ZIP contents for any violations
    with zipfile.ZipFile(ZIP_OUTPUT, "r") as zipf:
        for info in zipf.infolist():
            fname = os.path.basename(info.filename)
            if is_blacklisted(fname):
                raise RuntimeError(f"SECURITY/COPYRIGHT AUDIT FAILED: {info.filename} found in release archive!")

    zip_size_mb = os.path.getsize(ZIP_OUTPUT) / (1024 * 1024)
    print(f"[+] Distribution assembled successfully: {ZIP_OUTPUT} ({zip_size_mb:.2f} MB)")
    print("[+] Clean-room audit passed: ZERO proprietary assets packaged.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
