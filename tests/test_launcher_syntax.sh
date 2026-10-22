#!/usr/bin/env bash
set -euo pipefail

echo "[*] Running OnionOS Port Packaging & Syntax Validator..."

PORT_SHORTCUT="dist/Roms/PORTS/Shortcuts/Action/Risk of Rain.port"
LAUNCHER="dist/Roms/PORTS/Games/Risk of Rain/launch.sh"
CONTROLS="dist/Roms/PORTS/Games/Risk of Rain/properties/control.json"
README="dist/Roms/PORTS/Games/Risk of Rain/PUT_PC_FILES_HERE.txt"
BOXART="dist/Roms/PORTS/Imgs/Risk of Rain.png"
BINARY="dist/Roms/PORTS/Games/Risk of Rain/RiskOfRain"

# 1. Verify existence of canonical OnionOS shortcut and game files
test -f "$PORT_SHORTCUT" || { echo "FAIL: $PORT_SHORTCUT does not exist"; exit 1; }
test ! -f "dist/Roms/PORTS/Risk of Rain.port" || { echo "FAIL: dist/Roms/PORTS/Risk of Rain.port must not exist (only Shortcuts allowed)"; exit 1; }
test -f "$LAUNCHER" || { echo "FAIL: $LAUNCHER does not exist"; exit 1; }
test -f "$CONTROLS" || { echo "FAIL: $CONTROLS does not exist"; exit 1; }
test -f "$README" || { echo "FAIL: $README does not exist"; exit 1; }
test -f "$BOXART" || { echo "FAIL: $BOXART does not exist"; exit 1; }
test -f "$BINARY" || { echo "FAIL: $BINARY does not exist"; exit 1; }

# 2. Syntax validation
bash -n "$PORT_SHORTCUT" || { echo "FAIL: Syntax error in $PORT_SHORTCUT"; exit 1; }
bash -n "$LAUNCHER" || { echo "FAIL: Syntax error in $LAUNCHER"; exit 1; }

# 3. Strict check on .port to prevent OnionOS .notfound bug
# GameDataFile must appear EXACTLY once to prevent multi-line grep breakage in import.sh
COUNT=$(grep -c '^GameDataFile=' "$PORT_SHORTCUT")
if [ "$COUNT" -ne 1 ]; then
    echo "FAIL: GameDataFile occurs $COUNT times in $PORT_SHORTCUT (must be exactly 1)"; exit 1;
fi

grep -q '^GameDataFile="launch\.sh"' "$PORT_SHORTCUT" || { echo "FAIL: Invalid GameDataFile pattern in .port (must be launch.sh for autonomous first boot)"; exit 1; }
grep -q 'launch_standalone\.sh' "$PORT_SHORTCUT" || { echo "FAIL: launch_standalone.sh missing in .port"; exit 1; }
grep -q 'Games/Risk of Rain' "$PORT_SHORTCUT" || { echo "FAIL: Missing Games/Risk of Rain relative lookup in .port"; exit 1; }

# 4. Check launch.sh for dynamic hardware detection, overclocking, and resilient exit trap
grep -q 'IS_PLUS' "$LAUNCHER" || { echo "FAIL: launch.sh missing hardware detection"; exit 1; }
grep -q '/tmp/deviceModel' "$LAUNCHER" || { echo "FAIL: launch.sh missing OnionOS /tmp/deviceModel detection"; exit 1; }
grep -q '/customer/app/axp_test' "$LAUNCHER" || { echo "FAIL: launch.sh missing /customer/app/axp_test detection"; exit 1; }
grep -q 'CPU_FREQ=1800' "$LAUNCHER" || { echo "FAIL: launch.sh missing 1800 MHz MMP target"; exit 1; }
grep -q 'CPU_FREQ=1400' "$LAUNCHER" || { echo "FAIL: launch.sh missing 1400 MHz MM target"; exit 1; }
grep -q 'set_cpu 1200' "$LAUNCHER" || { echo "FAIL: launch.sh missing clock restoration to 1200"; exit 1; }
grep -q 'ondemand' "$LAUNCHER" || { echo "FAIL: launch.sh missing governor restore"; exit 1; }
grep -q 'trap cleanup EXIT INT TERM HUP' "$LAUNCHER" || { echo "FAIL: launch.sh missing full signal cleanup trap"; exit 1; }
grep -q 'audioserver' "$LAUNCHER" || { echo "FAIL: launch.sh missing audioserver handling"; exit 1; }
grep -q 'dd if=/dev/zero of=/dev/fb0' "$LAUNCHER" || { echo "FAIL: launch.sh missing framebuffer blanking on exit"; exit 1; }
grep -q 'sync' "$LAUNCHER" || { echo "FAIL: launch.sh missing sync buffer flush"; exit 1; }

# 5. Check controls mapping JSON
grep -q '"Y": "Ability 1 (Basic)"' "$CONTROLS" || { echo "FAIL: control.json missing Y ability 1"; exit 1; }
grep -q '"X": "Swap Item"' "$CONTROLS" || { echo "FAIL: control.json missing X swap item"; exit 1; }
grep -q '"A": "Enter / Confirm / Buy"' "$CONTROLS" || { echo "FAIL: control.json missing A enter/confirm"; exit 1; }
grep -q '"B": "Jump"' "$CONTROLS" || { echo "FAIL: control.json missing B jump"; exit 1; }
grep -q '"L1": "Ability 2"' "$CONTROLS" || { echo "FAIL: control.json missing L1 ability 2"; exit 1; }
grep -q '"R1": "Ability 4"' "$CONTROLS" || { echo "FAIL: control.json missing R1 ability 4"; exit 1; }
grep -q '"R2": "Ability 3"' "$CONTROLS" || { echo "FAIL: control.json missing R2 ability 3"; exit 1; }
grep -q '"L2": "Use Equipment Item"' "$CONTROLS" || { echo "FAIL: control.json missing L2 item"; exit 1; }

echo "[+] OnionOS port packaging syntax test PASSED."
exit 0
