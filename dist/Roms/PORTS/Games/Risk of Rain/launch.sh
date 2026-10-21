#!/bin/sh
# ==============================================================================
# Risk of Rain (2013) Native Launcher for OnionOS on Miyoo Mini / Mini Plus
# Native 640x480 port with NEON SIMD, Tremor audio, and lifecycle resilience
# ==============================================================================

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# Enable persistent execution log for debugging
LOG_FILE="$DIR/launch.log"
exec > "$LOG_FILE" 2>&1
echo "=== Risk of Rain Launcher Started ==="
date

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

echo "[*] Hardware: IS_PLUS=$IS_PLUS, Target CPU_FREQ=$CPU_FREQ MHz"

set_cpu() {
    if [ -f "/mnt/SDCARD/.tmp_update/bin/cpuclock" ]; then
        /mnt/SDCARD/.tmp_update/bin/cpuclock "$1" 2>/dev/null
    elif command -v cpuclock >/dev/null 2>&1; then
        cpuclock "$1" 2>/dev/null
    fi
}

set_cpu $CPU_FREQ

# 2. Release hardware audio device from audioserver for exclusive ALSA access
echo "[*] Releasing audio hardware..."
if [ -f "/mnt/SDCARD/.tmp_update/script/stop_audioserver.sh" ]; then
    /mnt/SDCARD/.tmp_update/script/stop_audioserver.sh 2>/dev/null || true
else
    killall -9 audioserver audioserver.mod 2>/dev/null || true
fi

# 2.1 Configure ALSA direct output
echo "[*] Configuring ALSA hardware routing..."
cat << 'EOF' > /tmp/asound.conf
pcm.!default {
    type plug
    slave.pcm {
        type hw
        card 0
        device 0
    }
}

ctl.!default {
    type hw
    card 0
}
EOF

# Copy asound configuration to home directories and set ALSA env vars
cp /tmp/asound.conf /tmp/.asoundrc 2>/dev/null || true
cp /tmp/asound.conf /root/.asoundrc 2>/dev/null || true
cp /tmp/asound.conf ~/.asoundrc 2>/dev/null || true
export ASOUNDRC=/tmp/.asoundrc

# 3. Clean exit trap: restore CPU clock, blank framebuffer, restore system audioserver, flush caches
cleanup() {
    echo "[*] Cleaning up and restoring state..."
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

    # 5. Ensure OnionOS audioserver is restored
    if [ -x "/customer/app/audioserver" ]; then
        /customer/app/audioserver &
    elif command -v audioserver >/dev/null 2>&1; then
        audioserver &
    fi

    echo "=== Risk of Rain Launcher Finished ==="
    sync
}
trap cleanup EXIT INT TERM HUP

# 4. Export library paths and SDL settings
export SDL_HIDE_BATTERY=1
export LD_LIBRARY_PATH="$DIR/libs:/mnt/SDCARD/Roms/PORTS/Games/Risk of Rain/libs:$LD_LIBRARY_PATH"
echo "[*] LD_LIBRARY_PATH: $LD_LIBRARY_PATH"

# 5. Check asset presence
echo "[*] Inspecting files in $DIR:"
ls -la "$DIR"

if [ ! -f "./data_mmp.win" ] && [ ! -f "./data.win" ]; then
    echo "[!] ERROR: Neither data_mmp.win nor data.win found in $DIR!"
    exit 1
fi

# 6. Launch native Risk of Rain runner
echo "[*] Checking binary ./RiskOfRain permissions:"
ls -la ./RiskOfRain
chmod +x ./RiskOfRain 2>/dev/null

DATA_FILE="./data_mmp.win"
if [ ! -f "$DATA_FILE" ]; then
    DATA_FILE="./data.win"
fi

echo "[*] Executing ./RiskOfRain with $DATA_FILE..."
./RiskOfRain "$DATA_FILE"
RR_EXIT=$?
echo "[*] ./RiskOfRain exited with status: $RR_EXIT"
