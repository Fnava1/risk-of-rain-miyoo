#!/bin/sh
# Risk of Rain (2013) Native Launcher for OnionOS on Miyoo Mini Plus

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# 1. Overclock CPU safely to 1.8 GHz for Miyoo Mini Plus
cpuclock 1800 2>/dev/null || true

# 2. Isolate audio subsystem
killall -9 audioserver 2>/dev/null || true

# 3. Clean exit trap: restore CPU clock, restore system audioserver, flush caches
cleanup() {
    echo "[*] Restoring system state..."
    cpuclock 1200 2>/dev/null || true
    /customer/app/audioserver &
    sync
}
trap cleanup EXIT INT TERM

# 4. Export library paths
export LD_LIBRARY_PATH="$DIR/libs:$LD_LIBRARY_PATH"

# 5. Launch native Risk of Rain runner
./RiskOfRain > ror.log 2>&1
