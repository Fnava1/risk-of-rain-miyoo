#!/usr/bin/env python3
"""
tools/assemble_dist.py
Assembles and packages the OnionOS distribution for Risk of Rain (2013).
Creates the canonical Pure Ports Collection structure and generates a release ZIP.
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

def main():
    print("[*] Assembling OnionOS Ports Distribution...")

    # Ensure required directories exist
    os.makedirs(os.path.join(ROMS_DIR, "Imgs"), exist_ok=True)
    os.makedirs(os.path.join(ROMS_DIR, "Shortcuts", "Action"), exist_ok=True)
    os.makedirs(os.path.join(GAMES_DIR, "properties"), exist_ok=True)

    # 1. Copy binary if built
    bin_src = os.path.join(PROJECT_ROOT, "butterscotch", "build_miyoo", "butterscotch")
    bin_dst = os.path.join(GAMES_DIR, "RiskOfRain")
    if os.path.isfile(bin_src):
        print(f"[*] Staging and stripping binary: {bin_src} -> {bin_dst}")
        strip_cmd = "/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-strip"
        if os.path.isfile(strip_cmd):
            os.system(f"{strip_cmd} -s '{bin_src}' -o '{bin_dst}'")
        else:
            shutil.copy2(bin_src, bin_dst)
    elif not os.path.isfile(bin_dst):
        print("[!] Warning: RiskOfRain binary not found. Build with Docker before final packaging.")

    # 2. Package into release ZIP
    print(f"[*] Creating release archive: {ZIP_OUTPUT}")
    with zipfile.ZipFile(ZIP_OUTPUT, "w", zipfile.ZIP_DEFLATED) as zipf:
        roms_base = os.path.join(DIST_DIR, "Roms")
        for root, dirs, files in os.walk(roms_base):
            for file in files:
                file_path = os.path.join(root, file)
                rel_path = os.path.relpath(file_path, DIST_DIR)
                # Preserve POSIX executable bits for ELF and scripts
                zip_info = zipfile.ZipInfo(rel_path.replace("\\", "/"))
                with open(file_path, "rb") as f:
                    content = f.read()
                if file.endswith((".sh", ".port")) or file == "RiskOfRain":
                    zip_info.external_attr = 0o755 << 16  # rwxr-xr-x
                else:
                    zip_info.external_attr = 0o644 << 16  # rw-r--r--
                zipf.writestr(zip_info, content)

    print(f"[+] Distribution assembled successfully: {ZIP_OUTPUT}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
