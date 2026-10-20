#!/bin/sh
# ==============================================================================
# Risk of Rain (2013) Native Launcher for OnionOS on Miyoo Mini / Mini Plus
# Native 640x480 port with NEON SIMD, Tremor audio, and lifecycle resilience
# ==============================================================================

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# 1. Dynamic Hardware Detection & Safe Overclocking
# Miyoo Mini Plus: 1800 MHz (AXP223 PMIC, large battery, WiFi present)
# Miyoo Mini (v1/v2/v3/v4 non-Plus): 1400 MHz (conservative ceiling avoiding PMIC panic)
IS_PLUS=0
if [ -d /sys/class/net/wlan0 ] || grep -qi "plus" /proc/cmdline 2>/dev/null; then
    IS_PLUS=1
fi

if [ "$IS_PLUS" -eq 1 ]; then
    CPU_FREQ=1800
else
    CPU_FREQ=1400
fi

set_cpu() {
    if [ -f "/mnt/SDCARD/.tmp_update/bin/cpuclock" ]; then
        /mnt/SDCARD/.tmp_update/bin/cpuclock "$1" 2>/dev/null
    elif command -v cpuclock >/dev/null 2>&1; then
        cpuclock "$1" 2>/dev/null
    fi
}

set_cpu $CPU_FREQ

# 2. Isolate audio subsystem: kill audioserver to prevent MI_AO hardware conflict
killall -9 audioserver 2>/dev/null || true

# 3. Clean exit trap: restore CPU clock, blank framebuffer, restore system audioserver, flush caches
cleanup() {
    # 1. Flush filesystem buffers to SD card to prevent FAT32/exFAT corruption
    sync

    # 2. Clear framebuffer to prevent frozen screen artifacts on exit (MENU button)
    dd if=/dev/zero of=/dev/fb0 bs=640 count=480 2>/dev/null || true

    # 3. Restore CPU clock to 1200 MHz
    set_cpu 1200

    # 4. Restore CPU governor to ondemand
    if [ -f "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor" ]; then
        echo ondemand > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
    fi

    # 5. Restart OnionOS audioserver
    if [ -x "/customer/app/audioserver" ]; then
        /customer/app/audioserver &
    elif command -v audioserver >/dev/null 2>&1; then
        audioserver &
    fi
}
trap cleanup EXIT INT TERM HUP

# 4. Export library paths
export LD_LIBRARY_PATH="$DIR/libs:/mnt/SDCARD/Roms/PORTS/Games/Risk of Rain/libs:$LD_LIBRARY_PATH"

# 5. Check asset presence
if [ ! -f "./data_mmp.win" ] && [ ! -f "./data.win" ]; then
    echo "Error: Neither data_mmp.win nor data.win found in $DIR!"
    exit 1
fi

# 6. Launch native Risk of Rain runner
chmod +x ./RiskOfRain 2>/dev/null
./RiskOfRain
