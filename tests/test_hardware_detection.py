#!/usr/bin/env python3
"""
Test Hardware Detection for Miyoo Mini vs Miyoo Mini Plus in launch.sh.
Verifies the multi-tier detection logic handles:
1. /tmp/deviceModel == "354" (OnionOS MMP runtime) -> 1800 MHz
2. /tmp/deviceModel == "283" (OnionOS MM classic runtime) -> 1400 MHz
3. MMP with WiFi turned OFF (/sys/class/net/wlan0 missing, but /customer/app/axp_test present) -> 1800 MHz
4. I2C probe fallback (/mnt/SDCARD/.tmp_update/bin/axp 0 returns 0) -> 1800 MHz
5. Pure MM classic environment -> 1400 MHz
"""

import os
import sys
import shutil
import subprocess
import tempfile
import unittest

LAUNCHER_PATH = os.path.join(os.path.dirname(__file__), "..", "dist", "Roms", "PORTS", "Games", "Risk of Rain", "launch.sh")

# Extract the hardware detection script block from launch.sh
def extract_hardware_detection_block():
    with open(LAUNCHER_PATH, "r", encoding="utf-8") as f:
        lines = f.readlines()

    start_idx = -1
    end_idx = -1
    for i, line in enumerate(lines):
        if "IS_PLUS=0" in line:
            start_idx = i
        if start_idx != -1 and "CPU_FREQ=1400" in line and "fi" in lines[i + 1]:
            end_idx = i + 2
            break

    if start_idx == -1 or end_idx == -1:
        raise ValueError("Could not find hardware detection block in launch.sh")

    return "".join(lines[start_idx:end_idx])


WORKSPACE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TMP_BASE = os.path.join(WORKSPACE_ROOT, ".tmp_tests")


class TestHardwareDetection(unittest.TestCase):

    def setUp(self):
        os.makedirs(TMP_BASE, exist_ok=True)
        self.test_dir = tempfile.mkdtemp(dir=TMP_BASE)
        # Convert host path to docker container path (/root/workspace/...)
        rel_path = os.path.relpath(self.test_dir, WORKSPACE_ROOT).replace("\\", "/")
        self.container_test_dir = f"/root/workspace/{rel_path}"
        self.code_block = extract_hardware_detection_block()

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def run_detection_in_env(self, env_setup_script):
        """
        Executes the detection script in a sandboxed bash environment.
        On Linux/CI, executes natively in bash. On Windows, executes inside Docker.
        """
        if sys.platform != "win32":
            root_dir = self.test_dir
            cmd_prefix = ["bash", "-c"]
        else:
            root_dir = self.container_test_dir
            cmd_prefix = ["docker", "exec", "determined_moore", "bash", "-c"]

        script = f"""
set -e
ROOT="{root_dir}"
cd "$ROOT"

# Fake environment
{env_setup_script}

# Hardware detection code to test (with adjusted paths for sandbox)
{self.code_block.replace('/tmp/deviceModel', '$ROOT/tmp/deviceModel')
                .replace('/customer/app/axp_test', '$ROOT/customer/app/axp_test')
                .replace('/mnt/SDCARD/.tmp_update/bin/axp', '$ROOT/bin/axp')
                .replace('/proc/cmdline', '$ROOT/proc/cmdline')
                .replace('/sys/class/net/wlan0', '$ROOT/sys/class/net/wlan0')}

echo "RESULT:IS_PLUS=$IS_PLUS:CPU_FREQ=$CPU_FREQ"
"""
        cmd = cmd_prefix + [script]
        try:
            res = subprocess.run(cmd, capture_output=True, text=True, check=True)
            for line in res.stdout.splitlines():
                if line.startswith("RESULT:"):
                    parts = line.strip().split(":")
                    is_plus = int(parts[1].split("=")[1])
                    cpu_freq = int(parts[2].split("=")[1])
                    return is_plus, cpu_freq
            self.fail(f"Could not find RESULT in output: {res.stdout}")
        except subprocess.CalledProcessError as e:
            self.fail(f"Sandbox script failed: {e.stderr}")

    def test_mmp_with_wifi_on(self):
        setup = """
mkdir -p "$ROOT/tmp" "$ROOT/customer/app" "$ROOT/sys/class/net/wlan0"
echo "354" > "$ROOT/tmp/deviceModel"
touch "$ROOT/customer/app/axp_test"
"""
        is_plus, cpu_freq = self.run_detection_in_env(setup)
        self.assertEqual(is_plus, 1)
        self.assertEqual(cpu_freq, 1800)

    def test_mmp_with_wifi_off_device_model_present(self):
        # wlan0 is missing, but OnionOS /tmp/deviceModel is 354
        setup = """
mkdir -p "$ROOT/tmp" "$ROOT/customer/app"
echo "354" > "$ROOT/tmp/deviceModel"
touch "$ROOT/customer/app/axp_test"
"""
        is_plus, cpu_freq = self.run_detection_in_env(setup)
        self.assertEqual(is_plus, 1)
        self.assertEqual(cpu_freq, 1800)

    def test_mmp_with_wifi_off_no_tmp_model_fallback_to_axp_test(self):
        # /tmp/deviceModel is missing (non-Onion or wiped), but NAND /customer/app/axp_test is present
        setup = """
mkdir -p "$ROOT/customer/app"
touch "$ROOT/customer/app/axp_test"
"""
        is_plus, cpu_freq = self.run_detection_in_env(setup)
        self.assertEqual(is_plus, 1)
        self.assertEqual(cpu_freq, 1800)

    def test_mmp_i2c_bus_probe_fallback(self):
        # /tmp/deviceModel and axp_test missing, but axp probe succeeds (returns 0)
        setup = """
mkdir -p "$ROOT/bin"
cat << 'EOF' > "$ROOT/bin/axp"
#!/bin/sh
exit 0
EOF
chmod +x "$ROOT/bin/axp"
"""
        is_plus, cpu_freq = self.run_detection_in_env(setup)
        self.assertEqual(is_plus, 1)
        self.assertEqual(cpu_freq, 1800)

    def test_mmp_cmdline_fallback(self):
        setup = """
mkdir -p "$ROOT/proc"
echo "console=ttyS0,115200 root=/dev/mtdblock3 board=miyoominiplus" > "$ROOT/proc/cmdline"
"""
        is_plus, cpu_freq = self.run_detection_in_env(setup)
        self.assertEqual(is_plus, 1)
        self.assertEqual(cpu_freq, 1800)

    def test_classic_miyoo_mini_onion(self):
        # OnionOS on classic Miyoo Mini: /tmp/deviceModel is 283
        setup = """
mkdir -p "$ROOT/tmp"
echo "283" > "$ROOT/tmp/deviceModel"
"""
        is_plus, cpu_freq = self.run_detection_in_env(setup)
        self.assertEqual(is_plus, 0)
        self.assertEqual(cpu_freq, 1400)

    def test_classic_miyoo_mini_pure(self):
        # No MMP indicators present anywhere
        setup = """
mkdir -p "$ROOT/proc"
echo "console=ttyS0,115200 root=/dev/mtdblock3" > "$ROOT/proc/cmdline"
"""
        is_plus, cpu_freq = self.run_detection_in_env(setup)
        self.assertEqual(is_plus, 0)
        self.assertEqual(cpu_freq, 1400)


if __name__ == "__main__":
    unittest.main()
