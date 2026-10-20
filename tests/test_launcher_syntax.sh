#!/usr/bin/env bash
set -euo pipefail

echo "[*] Running OnionOS Port Packaging & Syntax Validator..."

PORT_FILE="dist/Risk of Rain.port"
LAUNCHER="dist/Games/Risk of Rain/launch.sh"
CONTROLS="dist/Games/Risk of Rain/properties/control.json"

# 1. Verify existence of files
test -f "$PORT_FILE" || { echo "FAIL: $PORT_FILE does not exist"; exit 1; }
test -f "$LAUNCHER" || { echo "FAIL: $LAUNCHER does not exist"; exit 1; }
test -f "$CONTROLS" || { echo "FAIL: $CONTROLS does not exist"; exit 1; }

# 2. Strict syntax check on .port to prevent OnionOS .notfound bug
# Must contain exact GameDataFile definition without conditionals
grep -q '^GameDataFile="data_mmp.win"' "$PORT_FILE" || { echo "FAIL: Invalid GameDataFile definition in .port"; exit 1; }
if grep -qE "if \[|if test" "$PORT_FILE"; then
    echo "FAIL: Conditional statement found in .port file, will trigger .notfound bug!"; exit 1;
fi

# 3. Check launch.sh for CPU overclock and cleanup trap
grep -q "cpuclock 1800" "$LAUNCHER" || { echo "FAIL: launch.sh missing cpuclock 1800 overclock"; exit 1; }
grep -q "cpuclock 1200" "$LAUNCHER" || { echo "FAIL: launch.sh missing clock restoration"; exit 1; }
grep -q "trap cleanup EXIT INT TERM" "$LAUNCHER" || { echo "FAIL: launch.sh missing cleanup signal trap"; exit 1; }
grep -q "audioserver" "$LAUNCHER" || { echo "FAIL: launch.sh missing audioserver handling"; exit 1; }

# 4. Check controls mapping JSON
grep -q '"X": "Primary Attack"' "$CONTROLS" || { echo "FAIL: control.json missing X primary attack"; exit 1; }
grep -q '"A": "Jump"' "$CONTROLS" || { echo "FAIL: control.json missing A jump"; exit 1; }

echo "[+] OnionOS port packaging syntax test PASSED."
exit 0
