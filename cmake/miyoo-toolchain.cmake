set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Cross compiler paths inside Miyoo Mini toolchain
set(TOOLCHAIN_PREFIX arm-linux-gnueabihf)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)

# Hardened Miyoo Mini Plus flags (Cortex-A7 dual core, NEON SIMD, VFPv4, hard float, O3)
set(COMMON_ARCH_FLAGS "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -O3 -fomit-frame-pointer -ffast-math -pthread")

set(CMAKE_C_FLAGS "${COMMON_ARCH_FLAGS}" CACHE STRING "Miyoo C Flags" FORCE)
set(CMAKE_CXX_FLAGS "${COMMON_ARCH_FLAGS} -std=c++17" CACHE STRING "Miyoo CXX Flags" FORCE)

# Sysroot and library search locations
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
