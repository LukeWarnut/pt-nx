# Toolchain file for the Nintendo Switch 2 build (ounce-devkit: libounce, ouncevk), installed with devkitPro.
# Use: cmake -G Ninja -S switch -B build/switch2 -DCMAKE_TOOLCHAIN_FILE=cmake/switch2.cmake
#
# It is libounce's toolchain ($DEVKITPRO/libounce/cmake/Switch2.cmake: devkitA64, the NintendoSwitch platform with libounce as
# NX_ROOT, the Switch 2 CPU flags and __SWITCH2__) with the project's switches on. switch/CMakeLists.txt takes Vulkan from ouncevk
# in place of nxvk when PT_SWITCH2 is set.
if(NOT DEFINED ENV{DEVKITPRO})
  set(ENV{DEVKITPRO} "/opt/devkitpro")
endif()
set(PT_OUNCE_TOOLCHAIN "$ENV{DEVKITPRO}/libounce/cmake/Switch2.cmake")
if(NOT EXISTS "${PT_OUNCE_TOOLCHAIN}")
  message(FATAL_ERROR "${PT_OUNCE_TOOLCHAIN} not found: install ounce-devkit (dkp-pacman -S ounce-devkit)")
endif()
include("${PT_OUNCE_TOOLCHAIN}")

set(PT_SWITCH ON CACHE BOOL "Building for Nintendo Switch")
set(PT_SWITCH2 ON CACHE BOOL "Building for Nintendo Switch 2 (ounce-devkit)")
