#!/usr/bin/env bash
set -euo pipefail

echo "[*] Checking Miyoo Toolchain configuration..."

# 1. Verify required files exist
test -f "docker/Dockerfile.miyoo" || { echo "FAIL: docker/Dockerfile.miyoo missing"; exit 1; }
test -f "cmake/miyoo-toolchain.cmake" || { echo "FAIL: cmake/miyoo-toolchain.cmake missing"; exit 1; }
test -f "CMakeLists.txt" || { echo "FAIL: CMakeLists.txt missing"; exit 1; }

# 2. Verify toolchain compiler flags for Miyoo Mini Plus (Cortex-A7 NEON hard-float)
grep -q "cortex-a7" "cmake/miyoo-toolchain.cmake" || { echo "FAIL: cortex-a7 flag missing"; exit 1; }
grep -q "neon-vfpv4" "cmake/miyoo-toolchain.cmake" || { echo "FAIL: neon-vfpv4 flag missing"; exit 1; }
grep -q "mfloat-abi=hard" "cmake/miyoo-toolchain.cmake" || { echo "FAIL: hard-float flag missing"; exit 1; }

# 3. Verify target executable in CMakeLists.txt
grep -q "add_executable(RiskOfRain" "CMakeLists.txt" || { echo "FAIL: RiskOfRain target missing"; exit 1; }

echo "[+] Miyoo Mini Toolchain and CMake configuration verified successfully."
exit 0
