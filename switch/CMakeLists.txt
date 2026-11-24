# Nintendo Switch homebrew build of the P.T. port (a separate project, so the desktop build in ../CMakeLists.txt stays as it is).
#
#   cmake -G Ninja -S switch -B build/switch -DCMAKE_TOOLCHAIN_FILE=cmake/switch.cmake -DPT_NXVK_DIR=<nxvk package>
#   cmake --build build/switch            -> build/switch/pt.nro
#
# Graphics: the renderer is Vulkan, provided on the console by nxvk (NVK for the Switch, github.com/PalindromicBreadLoaf/nxvk),
# linked statically; PT_NXVK_DIR holds its package (lib/libnvk.a, lib/libnvk_support.a, vulkan/ headers). nxvk is GPL-2.0-or-later,
# so a binary built here that links it is a combined work under the GPL (switch/README.md).
cmake_minimum_required(VERSION 3.28)
project(pt_switch LANGUAGES C CXX)

if(NOT PT_SWITCH)
  message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=cmake/switch.cmake (path relative to the repository root)")
endif()
# Switch 2 (cmake/switch2.cmake, ounce-devkit): libounce in place of libnx, Vulkan from ouncevk in place of nxvk
option(PT_SWITCH2 "Build for Switch 2 with ounce-devkit (libounce, ouncevk)" OFF)
if(PT_SWITCH2)
  # cmake/switch.cmake passes PT_SWITCH in the flags; libounce's toolchain sets its own, so it is defined here
  add_compile_definitions(PT_SWITCH=1)
  # zlib, harfbuzz, freetype, libpng and bzip2 come from devkitPro's Switch portlibs (C only, they link into Switch 2 programs);
  # libounce's toolchain links that directory but puts only the Switch 2 portlibs on the include path
  include_directories(SYSTEM $ENV{DEVKITPRO}/portlibs/switch/include)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(PT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(PT_NXVK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../nxvk-pkg" CACHE PATH "The nxvk package: lib/libnvk.a, lib/libnvk_support.a, vulkan/, vk_video/")
set(PT_NXVK_SRC "${PT_ROOT}/../nxvk" CACHE PATH "The nxvk source (switch/smoke/nvk_compat.c supplies newlib gaps the driver needs)")
option(PT_RELEASE_LOCKS "Release game: the loop browser stays locked until the game was finished once" ON)

include(FetchContent)
cmake_policy(SET CMP0169 OLD)
set(FETCHCONTENT_QUIET ON)

# Vulkan headers matching volk, from the Khronos repository; nxvk's own headers come after them on the include path
FetchContent_Declare(vkheaders GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git GIT_TAG v1.4.350 GIT_SHALLOW TRUE)
FetchContent_Declare(volk GIT_REPOSITORY https://github.com/zeux/volk.git GIT_TAG 1.4.350 GIT_SHALLOW TRUE)
FetchContent_Declare(vma GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.3 GIT_SHALLOW TRUE)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.9b-docking GIT_SHALLOW TRUE)
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master GIT_SHALLOW TRUE)
FetchContent_Declare(lua51 URL https://www.lua.org/ftp/lua-5.1.5.tar.gz
  URL_HASH SHA256=2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333)
FetchContent_Declare(ogg GIT_REPOSITORY https://github.com/xiph/ogg.git GIT_TAG v1.3.5 GIT_SHALLOW TRUE)
FetchContent_Declare(vorbis GIT_REPOSITORY https://github.com/xiph/vorbis.git GIT_TAG v1.3.7 GIT_SHALLOW TRUE)
FetchContent_Declare(bc7enc URL https://codeload.github.com/richgel999/bc7enc_rdo/zip/b9438627eef73a1157e84201b6fa6eb2ffd6d9f0)
FetchContent_Declare(expat GIT_REPOSITORY https://github.com/libexpat/libexpat.git GIT_TAG R_2_6_4 GIT_SHALLOW TRUE)
set(PT_FETCH vkheaders volk vma glm imgui stb lua51 ogg vorbis bc7enc)
if(NOT PT_SWITCH2)
  list(APPEND PT_FETCH expat)  # for nxvk's driconf parsing
endif()
foreach(dep ${PT_FETCH})
  FetchContent_Populate(${dep})
endforeach()
# the Vulkan driver's own headers go after the Khronos ones; ouncevk's are on the toolchain's standard include path
if(PT_SWITCH2)
  set(PT_VK_DRIVER_INCLUDE)
else()
  set(PT_VK_DRIVER_INCLUDE ${PT_NXVK_DIR})
endif()

# ---- third party code -------------------------------------------------------------------------------------------------------

file(GLOB LUA51_SOURCES ${lua51_SOURCE_DIR}/src/*.c)
list(FILTER LUA51_SOURCES EXCLUDE REGEX ".*/(lua|luac|print)\\.c$")

add_library(pt_thirdparty STATIC
  ${volk_SOURCE_DIR}/volk.c
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
  ${LUA51_SOURCES}
  ${bc7enc_SOURCE_DIR}/bc7enc.cpp)
# the platform layer headers (SDL3/, dlfcn.h, imgui_impl_sdl3.h) come first, nxvk's vulkan/ headers over the Khronos ones
target_include_directories(pt_thirdparty PUBLIC
  ${PT_ROOT}/src/platform/switch
  ${PT_VK_DRIVER_INCLUDE}
  ${vkheaders_SOURCE_DIR}/include
  ${volk_SOURCE_DIR}
  ${vma_SOURCE_DIR}/include
  ${glm_SOURCE_DIR}
  ${imgui_SOURCE_DIR}
  ${lua51_SOURCE_DIR}/src
  ${stb_SOURCE_DIR}
  ${bc7enc_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}/generated/ogg-config
  /opt/devkitpro/portlibs/switch/include/harfbuzz
  /opt/devkitpro/portlibs/switch/include/freetype2
  # last: imgui_impl_sdl3.h resolves to the platform layer's replacement above, imgui_impl_vulkan.h to the real backend
  ${imgui_SOURCE_DIR}/backends)
# no LUA_USE_POSIX: it asks for _setjmp, which newlib does not have
target_compile_definitions(pt_thirdparty PUBLIC VK_NO_PROTOTYPES IMGUI_IMPL_VULKAN_USE_VOLK VK_USE_PLATFORM_VI_NN IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
target_compile_options(pt_thirdparty PRIVATE -w)

# Vorbis for the Wwise audio: the codebook library the desktop build downloads (hash checked), the sources compiled directly
set(PT_WWISE_CODEBOOKS_DIR ${CMAKE_BINARY_DIR}/_deps/ww2ogg)
set(PT_WWISE_CODEBOOKS_BIN ${PT_WWISE_CODEBOOKS_DIR}/packed_codebooks_aoTuV_603.bin)
set(PT_WWISE_CODEBOOKS_C ${PT_WWISE_CODEBOOKS_DIR}/wwise_codebooks.c)
if(NOT EXISTS ${PT_WWISE_CODEBOOKS_BIN})
  file(DOWNLOAD https://raw.githubusercontent.com/hcs64/ww2ogg/master/packed_codebooks_aoTuV_603.bin ${PT_WWISE_CODEBOOKS_BIN}
    EXPECTED_HASH SHA256=00a93eab267d281401b1efd54e888a2e183299b9e6c446c48d09f701a89d9d27 STATUS status)
  list(GET status 0 code)
  if(NOT code EQUAL 0)
    file(REMOVE ${PT_WWISE_CODEBOOKS_BIN})
    message(FATAL_ERROR "cannot download the Wwise Vorbis codebook library: ${status}")
  endif()
endif()
if(NOT EXISTS ${PT_WWISE_CODEBOOKS_C} OR ${PT_WWISE_CODEBOOKS_BIN} IS_NEWER_THAN ${PT_WWISE_CODEBOOKS_C})
  file(READ ${PT_WWISE_CODEBOOKS_BIN} hex HEX)
  string(LENGTH "${hex}" hex_length)
  math(EXPR size "${hex_length} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  file(WRITE ${PT_WWISE_CODEBOOKS_C}
    "#include <stddef.h>\nconst unsigned char pt_wwise_codebooks[] = {${bytes}};\nconst size_t pt_wwise_codebooks_size = ${size};\n")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/generated/ogg-config/ogg/config_types.h"
  "#ifndef __CONFIG_TYPES_H__\n#define __CONFIG_TYPES_H__\n#include <stdint.h>\ntypedef int16_t ogg_int16_t;\ntypedef uint16_t ogg_uint16_t;\ntypedef int32_t ogg_int32_t;\ntypedef uint32_t ogg_uint32_t;\ntypedef int64_t ogg_int64_t;\ntypedef uint64_t ogg_uint64_t;\n#endif\n")
set(VORBIS_SOURCES)
foreach(name mdct smallft block envelope window lsp lpc analysis synthesis psy info floor1 floor0 res0 mapping0 registry codebook
    sharedbook lookup bitrate)
  list(APPEND VORBIS_SOURCES ${vorbis_SOURCE_DIR}/lib/${name}.c)
endforeach()
add_library(pt_vorbis STATIC ${ogg_SOURCE_DIR}/src/bitwise.c ${ogg_SOURCE_DIR}/src/framing.c ${VORBIS_SOURCES} ${PT_WWISE_CODEBOOKS_C})
target_include_directories(pt_vorbis PUBLIC ${ogg_SOURCE_DIR}/include ${vorbis_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated/ogg-config
  PRIVATE ${vorbis_SOURCE_DIR}/lib)
target_compile_options(pt_vorbis PRIVATE -w)

# libexpat: nxvk's driconf parsing needs it and devkitPro ships no build of it
if(NOT PT_SWITCH2)
set(EXPAT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(EXPAT_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
add_subdirectory(${expat_SOURCE_DIR}/expat ${CMAKE_BINARY_DIR}/_deps/expat-build EXCLUDE_FROM_ALL)
# newlib declares arc4random_buf only under _GNU_SOURCE (C++ has it by default)
target_compile_definitions(expat PRIVATE _GNU_SOURCE)
endif()

# ---- generated headers (as ../CMakeLists.txt makes them) ----------------------------------------------------------------------

set(PT_VERSION "1.0.2")
set(PT_UPDATE_MANIFEST_URL "https://github.com/LoreanXavier/pt-pc/releases/latest/download/latest.json")
execute_process(COMMAND ${CMAKE_COMMAND} "-DOUT=${CMAKE_BINARY_DIR}/generated/pt_version.h" "-DPT_VERSION=${PT_VERSION}"
  "-DPT_UPDATE_MANIFEST_URL=${PT_UPDATE_MANIFEST_URL}" -P "${PT_ROOT}/cmake/version.cmake")
execute_process(COMMAND ${CMAKE_COMMAND} "-DOUT=${CMAKE_BINARY_DIR}/generated/build_id.h" "-DSOURCE=${PT_ROOT}" -P "${PT_ROOT}/cmake/build_id.cmake")

# ---- the engine ---------------------------------------------------------------------------------------------------------------

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS ${PT_ROOT}/src/engine/*.cpp)
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${PT_ROOT}/src/game/*.cpp)
# the desktop only parts are replaced by src/platform/switch/switch_stubs.cpp: vendor upscalers and frame generation, Streamline,
# OpenXR, voice recognition, virtual pads, HTTP, LiveSplit, child processes
set(EXCLUDED
  engine/xr/xr_host.cpp
  engine/voice/microphone.cpp engine/voice/voice_recognizer.cpp
  engine/platform/http.cpp engine/platform/os.cpp engine/platform/virtual_pad.cpp engine/platform/livesplit.cpp
  engine/render/upscale/dlss_backend.cpp engine/render/upscale/frame_generation.cpp engine/render/upscale/fsr_backend.cpp
  engine/render/upscale/streamline.cpp engine/render/upscale/xess_backend.cpp engine/render/upscale/upscale_platform.cpp)
set(SOURCES ${ENGINE_SOURCES} ${GAME_SOURCES})
foreach(path ${EXCLUDED})
  list(REMOVE_ITEM SOURCES ${PT_ROOT}/src/${path})
endforeach()
list(APPEND SOURCES
  ${PT_ROOT}/src/main.cpp
  ${PT_ROOT}/src/platform/switch/sdl_switch.cpp
  ${PT_ROOT}/src/platform/switch/switch_stubs.cpp
  ${PT_ROOT}/src/platform/switch/lsfg_bridge.cpp)
if(NOT PT_SWITCH2)
  list(APPEND SOURCES ${PT_NXVK_SRC}/switch/smoke/nvk_compat.c)
endif()

# LSFG frame generation (GPL-3.0-or-later, third_party/lsfg-vk): its shaders are read from the user's Lossless.dll at run time
file(GLOB_RECURSE LSFG_SOURCES CONFIGURE_DEPENDS ${PT_ROOT}/third_party/lsfg-vk/lsfg-vk-*/src/*.cpp)
add_library(pt_lsfg STATIC ${LSFG_SOURCES})
target_include_directories(pt_lsfg PUBLIC ${PT_ROOT}/third_party/lsfg-vk/lsfg-vk-backend/include ${PT_ROOT}/third_party/lsfg-vk/lsfg-vk-common/include
  PRIVATE ${PT_ROOT}/third_party/lsfg-vk/lsfg-vk-backend/src ${PT_ROOT}/third_party/lsfg-vk/lsfg-vk-common/src)
target_link_libraries(pt_lsfg PUBLIC pt_thirdparty)
target_compile_options(pt_lsfg PRIVATE -w -ffunction-sections -fdata-sections)

add_executable(pt ${SOURCES})
target_include_directories(pt PRIVATE ${PT_ROOT}/src ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(pt PRIVATE NOMINMAX GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS
  PT_SHADER_DIR="romfs:/shaders" PT_FONT_DIR="romfs:/fonts" PT_VOICE_MODEL_DIR="sdmc:/switch/pt/voice")
if(PT_RELEASE_LOCKS)
  target_compile_definitions(pt PRIVATE PT_RELEASE_LOCKS=1)
endif()
target_compile_definitions(pt PRIVATE PT_GAMEPLUS=1)
target_compile_options(pt PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(pt PRIVATE pt_lsfg pt_thirdparty pt_vorbis)

if(PT_SWITCH2)
  # ouncevk: libouncevk.a is one prelinked object that defines every vk* entry point, and volk (VK_NO_PROTOTYPES) defines the same
  # names as function pointers. The program links a copy whose vk* symbols are local to the driver object, with vkGetInstanceProcAddr
  # exported as vk_icdGetInstanceProcAddr, the entry point the platform layer and vk_context.cpp take from nxvk.
  set(PT_OUNCEVK_LIB "$ENV{DEVKITPRO}/portlibs/switch2/lib/libouncevk.a")
  if(NOT EXISTS ${PT_OUNCEVK_LIB})
    message(FATAL_ERROR "${PT_OUNCEVK_LIB} not found: install ounce-devkit (dkp-pacman -S ounce-devkit)")
  endif()
  set(PT_OUNCEVK_LOCAL ${CMAKE_BINARY_DIR}/ouncevk/libpt_ouncevk.a)
  add_custom_command(OUTPUT ${PT_OUNCEVK_LOCAL}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/ouncevk
    COMMAND ${CMAKE_OBJCOPY} --redefine-sym vkGetInstanceProcAddr=vk_icdGetInstanceProcAddr
            --wildcard --localize-symbol=vk[A-Z]* ${PT_OUNCEVK_LIB} ${PT_OUNCEVK_LOCAL}
    DEPENDS ${PT_OUNCEVK_LIB} VERBATIM)
  add_custom_target(pt_ouncevk DEPENDS ${PT_OUNCEVK_LOCAL})
  add_dependencies(pt pt_ouncevk)
  target_link_libraries(pt PRIVATE ${PT_OUNCEVK_LOCAL} -lharfbuzz -lfreetype -lpng16 -lbz2 -lz -lnx -lm -lstdc++ -pthread)
else()
# nxvk: link flags as the driver's README requires (whole archive, the entry point kept, gc of the rest)
set(NXVK_LIBS
  -L${PT_NXVK_DIR}/lib
  -Wl,--whole-archive -lnvk -Wl,--no-whole-archive
  -Wl,--start-group -lnvk_support -lz -Wl,--end-group
  -Wl,-u,vk_icdGetInstanceProcAddr -Wl,--gc-sections)
# expat after the driver archives that call it (static libraries resolve left to right)
target_link_libraries(pt PRIVATE ${NXVK_LIBS} expat -lharfbuzz -lfreetype -lpng16 -lbz2 -lz -lnx -lm -lstdc++ -pthread)
if(NOT EXISTS ${PT_NXVK_DIR}/lib/libnvk.a)
  message(WARNING "nxvk is not built yet (${PT_NXVK_DIR}/lib/libnvk.a missing): the objects compile, the link will fail until it is")
endif()
endif()

# ---- shaders (SPIR-V) ----------------------------------------------------------------------------------------------------------

find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin)
file(GLOB SHADERS CONFIGURE_DEPENDS ${PT_ROOT}/shaders/*.vert ${PT_ROOT}/shaders/*.frag ${PT_ROOT}/shaders/*.comp)
# shaders and fonts are built into the .nro (romfs:/), so it runs from nxlink or the menu without extra files; the game dump is the SD card's only part
set(ROMFS ${CMAKE_BINARY_DIR}/romfs)
set(SD_ROOT ${CMAKE_BINARY_DIR}/sdmc/switch/pt)
if(GLSLC)
  set(SPIRV)
  foreach(shader ${SHADERS})
    get_filename_component(name ${shader} NAME)
    set(out ${ROMFS}/shaders/${name}.spv)
    set(dep ${CMAKE_BINARY_DIR}/shader-deps/${name}.d)  # outside the romfs: the depfiles must not be packed into the .nro
    add_custom_command(OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${ROMFS}/shaders ${CMAKE_BINARY_DIR}/shader-deps
      COMMAND ${GLSLC} --target-env=vulkan1.3 -O -MD -MF ${dep} ${shader} -o ${out}
      DEPENDS ${shader} DEPFILE ${dep} VERBATIM)
    list(APPEND SPIRV ${out})
  endforeach()
  add_custom_target(pt_shaders ALL DEPENDS ${SPIRV})
else()
  message(WARNING "glslc not found (install the Vulkan SDK or set VULKAN_SDK): the shaders are not compiled, see switch/README.md")
endif()

# ---- FSR 2 (Switch 2) ---------------------------------------------------------------------------------------------------------
# AMD FidelityFX Super Resolution 2.2.1 (MIT, github.com/GPUOpen-Effects/FidelityFX-FSR2), built from source: the scene renders at the
# dynamic resolution's size and FSR 2 upscales it to the screen (1080p at most). Its Vulkan shader permutations are normally made by AMD's
# FidelityFX_SC.exe (Windows only); tools/fsr2_vk_shaders.py makes the same headers with glslc, for the permutations the engine uses.
# The runtime is MSVC-flavoured C++: src/platform/switch/fsr2/ffx_compat.h supplies what newlib lacks.
option(PT_FSR2 "Switch 2: build AMD FSR 2 (temporal upscaling of the scene to the screen)" ON)
if(PT_SWITCH2 AND PT_FSR2)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  if(NOT GLSLC)
    message(FATAL_ERROR "FSR 2 needs glslc for its shaders (install the Vulkan SDK, or configure with -DPT_FSR2=OFF)")
  endif()
  FetchContent_Declare(fsr2 URL https://github.com/GPUOpen-Effects/FidelityFX-FSR2/archive/1680d1edd5c034f88ebbbb793d8b88f8842cf804.tar.gz
    URL_HASH SHA256=19df8378ea882d437a6d61eeaa1c50b7e091fe409dd3029be8f0d819c7a89d75)
  FetchContent_Populate(fsr2)
  set(PT_FSR2_API ${fsr2_SOURCE_DIR}/src/ffx-fsr2-api)
  set(PT_FSR2_GEN ${CMAKE_BINARY_DIR}/fsr2-shaders)
  file(GLOB PT_FSR2_SHADER_SOURCES ${PT_FSR2_API}/shaders/*.h ${PT_FSR2_API}/shaders/*.glsl)
  set(PT_FSR2_HEADERS)
  foreach(pass tcr_autogen autogen_reactive accumulate compute_luminance_pyramid depth_clip lock reconstruct_previous_depth rcas)
    list(APPEND PT_FSR2_HEADERS ${PT_FSR2_GEN}/ffx_fsr2_${pass}_pass_permutations.h)
  endforeach()
  add_custom_command(OUTPUT ${PT_FSR2_HEADERS}
    COMMAND ${Python3_EXECUTABLE} -I ${PT_ROOT}/tools/fsr2_vk_shaders.py --glslc ${GLSLC} --src ${PT_FSR2_API}/shaders --out ${PT_FSR2_GEN}
    DEPENDS ${PT_ROOT}/tools/fsr2_vk_shaders.py ${PT_FSR2_SHADER_SOURCES}
    COMMENT "FSR 2 shader permutations"
    VERBATIM)
  add_library(pt_fsr2 STATIC
    ${PT_FSR2_API}/ffx_fsr2.cpp
    ${PT_FSR2_API}/ffx_assert.cpp
    ${PT_FSR2_API}/vk/ffx_fsr2_vk.cpp
    ${PT_FSR2_API}/vk/shaders/ffx_fsr2_shaders_vk.cpp
    ${PT_FSR2_HEADERS})
  set_target_properties(pt_fsr2 PROPERTIES CXX_STANDARD 17)  # std::codecvt_utf8_utf16 (C++17, removed later)
  target_include_directories(pt_fsr2 PUBLIC ${PT_FSR2_API} PRIVATE ${PT_FSR2_GEN})
  target_compile_definitions(pt_fsr2 PUBLIC FFX_GCC)
  target_compile_options(pt_fsr2 PRIVATE -w -include ${PT_ROOT}/src/platform/switch/fsr2/ffx_compat.h -ffunction-sections -fdata-sections)
  target_link_libraries(pt_fsr2 PUBLIC pt_thirdparty)
  target_link_libraries(pt PRIVATE pt_fsr2)
  target_compile_definitions(pt PRIVATE PT_WITH_FSR2=1)
endif()

# ---- .nro ---------------------------------------------------------------------------------------------------------------------

set(DEVKITPRO $ENV{DEVKITPRO})
# Both .nro files present the game as the original did: its title art as the icon (switch/assets/icon.jpg, via SteamGridDB, as Switch
# homebrew ports commonly use), "P.T." by 7780s Studio, the name it was released under, and its title picture as the start-up screen
# (switch/assets/cover.jpg; without it the game reads sce_sys/pic1.png from the player's dump). pt-public.nro (target pt_public_nro) is
# packed from the same program without test files, for releases.
set(PT_PRIVATE_ASSETS ${PT_ROOT}/switch/assets/private)
set(PT_NRO_ICON ${PT_ROOT}/switch/assets/icon.jpg)
set(PT_NRO_TITLE "P.T.")
set(PT_NRO_AUTHOR "7780s Studio")
if(EXISTS ${PT_ROOT}/switch/assets/cover.jpg)
  set(PT_COVER_COMMAND ${CMAKE_COMMAND} -E copy ${PT_ROOT}/switch/assets/cover.jpg ${ROMFS}/cover.jpg)
else()
  set(PT_COVER_COMMAND ${CMAKE_COMMAND} -E rm -f ${ROMFS}/cover.jpg)
endif()
add_custom_command(TARGET pt POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${ROMFS}/fonts ${ROMFS}/shaders ${SD_ROOT}/save
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PT_ROOT}/assets/fonts ${ROMFS}/fonts
  COMMAND ${PT_COVER_COMMAND}
  COMMAND ${DEVKITPRO}/tools/bin/nacptool --create ${PT_NRO_TITLE} ${PT_NRO_AUTHOR} "${PT_VERSION}" ${CMAKE_BINARY_DIR}/pt.nacp
  COMMAND ${DEVKITPRO}/tools/bin/elf2nro $<TARGET_FILE:pt> ${CMAKE_BINARY_DIR}/pt.nro --icon=${PT_NRO_ICON}
          --nacp=${CMAKE_BINARY_DIR}/pt.nacp --romfsdir=${ROMFS}
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/pt.nro ${SD_ROOT}/../pt.nro
  VERBATIM)
add_custom_target(pt_public_nro
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/romfs-public
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${ROMFS} ${CMAKE_BINARY_DIR}/romfs-public
  COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_BINARY_DIR}/romfs-public/test_env.txt
  COMMAND ${DEVKITPRO}/tools/bin/nacptool --create ${PT_NRO_TITLE} ${PT_NRO_AUTHOR} "${PT_VERSION}" ${CMAKE_BINARY_DIR}/pt-public.nacp
  COMMAND ${DEVKITPRO}/tools/bin/elf2nro $<TARGET_FILE:pt> ${CMAKE_BINARY_DIR}/pt-public.nro --icon=${PT_NRO_ICON}
          --nacp=${CMAKE_BINARY_DIR}/pt-public.nacp --romfsdir=${CMAKE_BINARY_DIR}/romfs-public
  DEPENDS pt
  VERBATIM)
