# gtav - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(gtav LANGUAGES CXX)

# Build id for bug reports: the source commit (git describe), or "unknown" outside a git checkout.
execute_process(COMMAND git describe --always --dirty --tags
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
                OUTPUT_VARIABLE GTAV_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT GTAV_BUILD_ID)
    set(GTAV_BUILD_ID "unknown")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GTAV_GAME_DLL: the release layout. The exe holds no recompiled game code; it loads gtav_game.dll,
# which the player's installer generates from their own discs and builds with the bundled clang
# (fs/gtav_pch.h). The exe then needs no generated sources at all.
option(GTAV_GAME_DLL "Build gtav_fast.exe against a separately built gtav_game.dll" OFF)

# iOS: the SDK installs runtime libraries only; the codegen tool is the Mac's (it only runs when
# the generated code is out of date, which a phone build never regenerates).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(GTAV_IOS TRUE)
    find_package(rexglue 0.10.0 REQUIRED CONFIG)
    if(NOT TARGET rex::rexglue)
        set(GTAV_HOST_REXGLUE "${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/out/install/mac-arm64/bin/rexglue"
            CACHE FILEPATH "Host (macOS) rexglue codegen tool")
        add_executable(rex::rexglue IMPORTED)
        set_target_properties(rex::rexglue PROPERTIES IMPORTED_LOCATION "${GTAV_HOST_REXGLUE}")
    endif()
endif()

if(GTAV_GAME_DLL)
    find_package(rexglue 0.10.0 REQUIRED CONFIG)
else()
    include(generated/rexglue.cmake)
endif()

# The SDK installs its codegen tool for Release only; the Windows SDK is a RelWithDebInfo build.
# (On Linux the SDK is built in Release, so rex::rexglue resolves without this.)
if(WIN32 AND TARGET rex::rexglue)
    # rexglueConfig maps every config to Release, so give Release a location (copied into <prefix>/bin).
    get_filename_component(_rex_prefix "${rexglue_DIR}/../../.." ABSOLUTE)
    set_property(TARGET rex::rexglue APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
    set_target_properties(rex::rexglue PROPERTIES
        IMPORTED_LOCATION_RELEASE "${_rex_prefix}/bin/rexgluerd.exe")
endif()

# Cross builds (Android): codegen runs on the build host, with the host SDK's rexglue tool.
if(ANDROID AND TARGET rex::rexglue)
    set(GTAV_HOST_REXGLUE "${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/out/install/linux-amd64/bin/rexglue"
        CACHE FILEPATH "Host rexglue binary used for codegen in cross builds")
    set_target_properties(rex::rexglue PROPERTIES IMPORTED_LOCATION_RELEASE "${GTAV_HOST_REXGLUE}"
                          IMPORTED_LOCATION "${GTAV_HOST_REXGLUE}")
endif()

# Sources
set(GTAV_SOURCES
    src/main.cpp
    src/crash_handler.cpp
    src/cutscene_audio_fix.cpp
    src/crash_guards.cpp
    src/script_timer_fix.cpp
    src/pc_controls.cpp
    src/bug_report.cpp
    src/frame_limit.cpp
    src/wait_profile.cpp
    src/gpu_fence_wait.cpp
    src/linux_threads.cpp
    src/native/stats.cpp
    src/native/capture.cpp
    src/native/d3d_probe.cpp
    src/native/d3d_hooks.cpp
    src/native/renderer.cpp
    src/native/shaders.cpp
    src/native/menu.cpp
    src/native/controls.cpp
    src/native/backend.cpp
    src/native/deferred_list.cpp
    src/native/textures.cpp
    src/native/write_watch.cpp
    src/native/shader_compiler.cpp
    src/native/touch_controls.cpp
)

if(WIN32)
    add_executable(gtav WIN32 ${GTAV_SOURCES})
elseif(ANDROID)
    # Android: the game is libmain.so, which SDL's SDLActivity loads and whose SDL_main it calls
    # (tools/android). A shared library exports the rex_skip_rendering_* hooks by default.
    add_library(gtav SHARED ${GTAV_SOURCES})
    if(GTAV_GAME_DLL)
        # Release layout: libgtav_fast.so (no game code) is loaded by libgtav_game.so, which the
        # player's install builds from their own discs; libmain.so (src/android_main_shim.cpp)
        # opens it from the app's files folder and calls SDL_main here.
        set_target_properties(gtav PROPERTIES OUTPUT_NAME gtav_fast)
        add_library(gtav_main_shim SHARED src/android_main_shim.cpp)
        set_target_properties(gtav_main_shim PROPERTIES OUTPUT_NAME main)
        target_link_libraries(gtav_main_shim PRIVATE rex::runtime log)
        # libgtav_setup.so: the setup screen's game data install (no game code, no SDK).
        add_library(gtav_setup SHARED src/android_setup_jni.cpp ../loader/src/install.cpp
                    ../loader/src/sources.cpp src/native/adreno_shader_fix.cpp)
        target_link_libraries(gtav_setup PRIVATE log)
    else()
        set_target_properties(gtav PROPERTIES OUTPUT_NAME main)
    endif()
    target_link_libraries(gtav PRIVATE android log)
    target_sources(gtav PRIVATE src/android_threads.cpp)
    target_link_options(gtav PRIVATE -Wl,--wrap=pthread_create)
else()
    add_executable(gtav ${GTAV_SOURCES})
    # The SDK's Vulkan command processor finds rex_skip_rendering_swap with dlsym.
    # (macOS executables export their global symbols by default.)
    if(NOT APPLE)
        target_link_options(gtav PRIVATE -Wl,--export-dynamic-symbol=rex_skip_rendering_swap
                            -Wl,--export-dynamic-symbol=rex_skip_rendering_fence
                            -Wl,--export-dynamic-symbol=rex_skip_rendering_idle
                            -Wl,--export-dynamic-symbol=rex_skip_rendering_gpu_waiters)
    endif()
endif()

if(GTAV_GAME_DLL)
    target_sources(gtav PRIVATE src/game_bridge.cpp)
    target_compile_definitions(gtav PRIVATE GTAV_GAME_DLL=1)
    target_include_directories(gtav PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(gtav PRIVATE rex::runtime)
    rexglue_configure_target(gtav)
    # Pre-link: list the hooks and the game functions the exe calls (names only), build the import
    # library for gtav_game.dll and the export list the DLL links against (gtav_fast.def).
    # Linux: a stub gtav_game.so to link against, the exe's dynamic list and the .so's version script.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(GTAV_INTERFACE_DIR ${CMAKE_BINARY_DIR}/game_interface)
    if(WIN32)
        get_filename_component(GTAV_LLVM_BIN "${CMAKE_LINKER}" DIRECTORY)
        add_custom_command(TARGET gtav PRE_LINK
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/game_dll/gen_interface.py
                    ${GTAV_LLVM_BIN} ${CMAKE_BINARY_DIR}/CMakeFiles/gtav.dir ${GTAV_INTERFACE_DIR}
                    $<TARGET_FILE:rex::runtime>
            VERBATIM)
        target_link_options(gtav PRIVATE "LINKER:/DEF:${GTAV_INTERFACE_DIR}/gtav_fast.def"
                            "LINKER:${GTAV_INTERFACE_DIR}/gtav_game.lib")
    elseif(APPLE)
        # macOS: link against a stub gtav_game.dylib (@rpath = the exe folder); -export_dynamic
        # keeps the hooks and the bridge, which only the game library calls, in the export trie.
        find_program(GTAV_LLVM_NM NAMES llvm-nm nm REQUIRED)
        get_filename_component(GTAV_LLVM_BIN "${GTAV_LLVM_NM}" DIRECTORY)
        add_custom_command(TARGET gtav PRE_LINK
            COMMAND ${CMAKE_COMMAND} -E env GTAV_STUB_SDK=$<IF:$<BOOL:${GTAV_IOS}>,iphoneos,macosx>
                    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/game_dll/gen_interface.py
                    ${GTAV_LLVM_BIN} ${CMAKE_BINARY_DIR}/CMakeFiles/gtav.dir ${GTAV_INTERFACE_DIR}
                    $<TARGET_FILE:rex::runtime> ${CMAKE_CXX_COMPILER}
            VERBATIM)
        target_link_options(gtav PRIVATE "${GTAV_INTERFACE_DIR}/stub/gtav_game.dylib"
                            "LINKER:-export_dynamic")
        set_target_properties(gtav PROPERTIES OUTPUT_NAME gtav_fast)
    elseif(ANDROID)
        # Android: no stub. The game functions stay undefined in libgtav_fast.so and bind when
        # libmain.so dlopens libgtav_game.so: bionic resolves them in the dlopen'd tree (game ->
        # libgtav_fast -> librexruntime), so both directions link at load (-z now).
        get_filename_component(GTAV_LLVM_BIN "${CMAKE_NM}" DIRECTORY)
        add_custom_command(TARGET gtav PRE_LINK
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/game_dll/gen_interface.py
                    ${GTAV_LLVM_BIN} ${CMAKE_BINARY_DIR}/CMakeFiles/gtav.dir ${GTAV_INTERFACE_DIR}
                    $<TARGET_FILE:rex::runtime> --android
            VERBATIM)
        target_link_options(gtav PRIVATE -Wl,-z,undefs)
    else()
        find_program(GTAV_LLVM_NM NAMES llvm-nm llvm-nm-20 llvm-nm-19 REQUIRED)
        get_filename_component(GTAV_LLVM_BIN "${GTAV_LLVM_NM}" DIRECTORY)
        add_custom_command(TARGET gtav PRE_LINK
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/game_dll/gen_interface.py
                    ${GTAV_LLVM_BIN} ${CMAKE_BINARY_DIR}/CMakeFiles/gtav.dir ${GTAV_INTERFACE_DIR}
                    $<TARGET_FILE:rex::runtime> ${CMAKE_CXX_COMPILER}
            VERBATIM)
        target_link_options(gtav PRIVATE "LINKER:--dynamic-list=${GTAV_INTERFACE_DIR}/gtav_fast.dynlist"
                            "${GTAV_INTERFACE_DIR}/stub/gtav_game.so")
        set_target_properties(gtav PROPERTIES OUTPUT_NAME gtav_fast)
    endif()
else()
    rexglue_setup_target(gtav)
endif()

# The SDK builds recompiled code with -mcmodel=large ("large executable support"): every call
# between the ~100 MB of game functions then goes through a 64-bit address in a register
# (movabs + indirect call), and the game makes calls constantly (__savegprlr/__restgprlr in most
# functions). The code is far below 2 GB, so the default model (direct calls) works. x86-64-v3
# (Haswell / Excavator and newer) adds MOVBE (a big-endian guest load or store is one
# instruction), BMI2 and AVX2. GTAV_X86_64_V3=OFF builds for any x86-64 with SSE4.1.
if(UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    option(GTAV_X86_64_V3 "Build the game code for x86-64-v3 CPUs (MOVBE, BMI2, AVX2)" ON)
    foreach(_gtav_target gtav gtav_recomp)
        if(NOT TARGET ${_gtav_target})
            continue()
        endif()
        target_compile_options(${_gtav_target} PRIVATE -mcmodel=small)
        if(GTAV_X86_64_V3)
            target_compile_options(${_gtav_target} PRIVATE -march=x86-64-v3)
        endif()
    endforeach()
elseif(ANDROID)
    # Phones: the SDK builds for baseline armv8-a. ARMv8.2 adds LSE atomics (one instruction
    # instead of an LL/SC loop for every guest lwarx/stwcx. pair and host lock), RCpc and dot
    # product: every Snapdragon 845 (2018) and newer core has them.
    set(GTAV_ARM64_MARCH "armv8.2-a+lse+rcpc+dotprod" CACHE STRING "-march for the Android game code")
    # The game is a shared library here (an executable elsewhere): every call between the ~100k
    # recompiled functions went through the PLT (10% of the game's render thread). Bind them locally.
    foreach(_gtav_target gtav gtav_recomp)
        if(TARGET ${_gtav_target})
            target_compile_options(${_gtav_target} PRIVATE -march=${GTAV_ARM64_MARCH} -fno-semantic-interposition)
        endif()
    endforeach()
    target_link_options(gtav PRIVATE -Wl,-Bsymbolic-functions)
elseif(WIN32 AND NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    # Windows: the SDK already uses the default code model; only the CPU level is added.
    option(GTAV_X86_64_V3 "Build the game code for x86-64-v3 CPUs (MOVBE, BMI2, AVX2)" ON)
    if(GTAV_X86_64_V3)
        foreach(_gtav_target gtav gtav_recomp)
            if(TARGET ${_gtav_target})
                target_compile_options(${_gtav_target} PRIVATE -march=x86-64-v3)
            endif()
        endforeach()
    endif()
endif()

# The SDK exports REXGLUE_ENABLE_PROFILING / _PERF_COUNTERS to every non-Release build: each
# indirect guest call then does an atomic add on one shared counter in rexruntime (~3% of every
# game thread in a Windows profile, plus cache-line traffic between them). Off for play.
option(GTAV_SDK_COUNTERS "Keep the SDK's guest call counters in the game code" OFF)
if(NOT GTAV_SDK_COUNTERS)
    foreach(_gtav_target gtav gtav_recomp)
        if(TARGET ${_gtav_target})
            target_compile_options(${_gtav_target} PRIVATE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
        endif()
    endforeach()
endif()

# The generated registration function (one call per game function, ~100k) takes clang's arm64
# backend hours (-O3, and GlobalISel at -O0); it runs once at startup: unoptimized, SelectionDAG.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/generated/default/gtav_register.cpp
                            PROPERTIES COMPILE_OPTIONS "-O0;-fno-global-isel" SKIP_PRECOMPILE_HEADERS ON)

# Profile-guided optimization of the recompiled game code (clang): GTAV_PGO=GENERATE builds an
# instrumented game (writes LLVM_PROFILE_FILE when it exits normally or on the profile flush); merge
# the .profraw files with llvm-profdata into GTAV_PGO_PROFILE and build with GTAV_PGO=USE.
set(GTAV_PGO "OFF" CACHE STRING "Profile-guided optimization of the game code: OFF | GENERATE | USE")
set(GTAV_PGO_PROFILE "" CACHE FILEPATH "Merged .profdata for GTAV_PGO=USE")
if(GTAV_PGO STREQUAL "GENERATE")
    target_compile_options(gtav_recomp PRIVATE -fprofile-instr-generate)
    target_link_options(gtav PRIVATE -fprofile-instr-generate)
    target_sources(gtav PRIVATE src/pgo_dump.cpp)
elseif(GTAV_PGO STREQUAL "USE")
    if(NOT EXISTS "${GTAV_PGO_PROFILE}")
        message(FATAL_ERROR "GTAV_PGO=USE needs GTAV_PGO_PROFILE (a merged .profdata)")
    endif()
    target_compile_options(gtav_recomp PRIVATE "-fprofile-instr-use=${GTAV_PGO_PROFILE}"
                           -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date)
endif()

# Native renderer: Plume (Vulkan, SDL3 surface). SDL3 itself comes from librexruntime, which
# exports it, so only the headers are needed here.
# Windows: rexruntime.dll exports none of its static SDL3, so the native window is Win32 (Plume's
# HWND path) and no SDL is linked here.
if(NOT WIN32)
    find_package(SDL3 REQUIRED CONFIG COMPONENTS Headers)
    set(PLUME_SDL3_VULKAN_ENABLED ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/plume ${CMAKE_BINARY_DIR}/plume EXCLUDE_FROM_ALL)
set_target_properties(plume PROPERTIES POSITION_INDEPENDENT_CODE ON)
option(GTAV_NATIVE_VALIDATION "Enable the Vulkan validation layer on the native renderer's instance" OFF)
if(GTAV_NATIVE_VALIDATION)
    target_compile_definitions(plume PRIVATE VULKAN_VALIDATION_LAYER_ENABLED)
endif()
target_link_libraries(gtav PRIVATE plume)

# The texture decoder uses the SDK's Xenos texture helpers (TextureInfo, tiling), which live in
# the xenos GPU plugin library.
# On Windows the plugin DLL exports only its factory, so those sources are compiled in instead.
if(WIN32)
    set(REX_SDK_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../rexglue-sdk/src" CACHE PATH "ReXGlue SDK source tree")
    target_sources(gtav PRIVATE
        ${REX_SDK_SRC}/graphics/pipeline/texture/info.cpp
        ${REX_SDK_SRC}/graphics/pipeline/texture/info_formats.cpp
        ${REX_SDK_SRC}/graphics/pipeline/texture/util.cpp
        ${REX_SDK_SRC}/graphics/xenos.cpp)
    target_compile_definitions(gtav PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(gtav PRIVATE winmm)
    # rex_shader_float_constant_bitmap and rex_skip_rendering_wake are dllexported by the GPU plugin.
    find_library(REXGPU_XENOS NAMES rexgpu-xenosrd rexgpu-xenos PATHS ${rexglue_DIR}/../.. NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(gtav PRIVATE ${REXGPU_XENOS})
else()
    find_library(REXGPU_XENOS rexgpu-xenos PATHS ${rexglue_DIR}/../.. NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(gtav PRIVATE ${REXGPU_XENOS})
endif()

# Native renderer's own shaders (present blit), compiled to SPIR-V headers with XenosRecomp's DXC.
set(GTAV_DXC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/XenosRecomp/thirdparty/dxc-bin)
if(WIN32)
    set(GTAV_DXC ${GTAV_DXC_DIR}/bin/x64/dxc.exe)
elseif(APPLE)
    set(GTAV_DXC ${CMAKE_COMMAND} -E env DYLD_LIBRARY_PATH=${GTAV_DXC_DIR}/lib/arm64 ${GTAV_DXC_DIR}/bin/arm64/dxc-macos)
else()
    set(GTAV_DXC ${CMAKE_COMMAND} -E env LD_LIBRARY_PATH=${GTAV_DXC_DIR}/lib/x64 ${GTAV_DXC_DIR}/bin/x64/dxc-linux)
endif()
set(GTAV_NATIVE_SHADER_DIR ${CMAKE_BINARY_DIR}/native_shaders)
set(GTAV_BLIT_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/native/shaders/native_blit.hlsl)
set(GTAV_RECTLIST_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/native/shaders/native_rectlist.hlsl)
set(GTAV_MEMORY_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/native/shaders/native_memory.hlsl)
set(GTAV_MENU_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/native/shaders/native_menu.hlsl)
# Optional: copy prebuilt headers from GTAV_PREBUILT_NATIVE_SHADERS instead of running dxc.
set(GTAV_PREBUILT_NATIVE_SHADERS "" CACHE PATH "Directory of prebuilt native shader headers (skips dxc)")
if(GTAV_PREBUILT_NATIVE_SHADERS)
    file(GLOB _gtav_prebuilt ${GTAV_PREBUILT_NATIVE_SHADERS}/*.h)
    file(COPY ${_gtav_prebuilt} DESTINATION ${GTAV_NATIVE_SHADER_DIR})
else()
add_custom_command(
    OUTPUT ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GTAV_NATIVE_SHADER_DIR}
    COMMAND ${GTAV_DXC}
            -T vs_6_0 -E VSMain -spirv -fspv-target-env=vulkan1.1 -Vn g_native_blit_vs
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_blit_vs.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSMain -spirv -fspv-target-env=vulkan1.1 -Vn g_native_blit_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_blit_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSDepth -spirv -fspv-target-env=vulkan1.1 -Vn g_native_depth_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_depth_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSResolve -spirv -fspv-target-env=vulkan1.1 -Vn g_native_resolve_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_resolve_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSDepthPack -spirv -fspv-target-env=vulkan1.1 -Vn g_native_depth_pack_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_depth_pack_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T gs_6_0 -E main -spirv -fspv-target-env=vulkan1.1 -Vn g_native_rectlist_gs
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_rectlist_gs.h ${GTAV_RECTLIST_HLSL}
    COMMAND ${GTAV_DXC}
            -T cs_6_0 -E CSResolveToMemory -spirv -fspv-target-env=vulkan1.1 -Vn g_native_resolve_memory_cs
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_resolve_memory_cs.h ${GTAV_MEMORY_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSDigits -spirv -fspv-target-env=vulkan1.1 -Vn g_native_digits_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_digits_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSAlpha -spirv -fspv-target-env=vulkan1.1 -Vn g_native_alpha_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_alpha_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${GTAV_DXC}
            -T vs_6_0 -E VSMenu -HV 2021 -spirv -Vn g_native_menu_vs
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_menu_vs.h ${GTAV_MENU_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSMenu -HV 2021 -spirv -Vn g_native_menu_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_menu_ps.h ${GTAV_MENU_HLSL}
    COMMAND ${GTAV_DXC}
            -T ps_6_0 -E PSDiagFlat -spirv -fspv-target-env=vulkan1.1 -Vn g_native_diag_flat_ps
            -Fh ${GTAV_NATIVE_SHADER_DIR}/native_diag_flat_ps.h ${GTAV_BLIT_HLSL}
    COMMAND ${CMAKE_COMMAND} -E echo "#pragma once" > ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_digits_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_diag_flat_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_alpha_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_resolve_memory_cs.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_rectlist_gs.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_depth_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_resolve_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_depth_pack_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_blit_vs.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_blit_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_menu_vs.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    COMMAND ${CMAKE_COMMAND} -E echo "#include \"native_menu_ps.h\"" >> ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
    DEPENDS ${GTAV_BLIT_HLSL} ${GTAV_RECTLIST_HLSL} ${GTAV_MEMORY_HLSL} ${GTAV_MENU_HLSL}
    VERBATIM)
if(ANDROID)
    # No shaderInt64 on Adreno: lower the 64-bit address math of the device-address shaders.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT ${GTAV_NATIVE_SHADER_DIR}/lowered.stamp
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/android/lower_header.py
                ${GTAV_NATIVE_SHADER_DIR}/native_resolve_memory_cs.h
        COMMAND ${CMAKE_COMMAND} -E touch ${GTAV_NATIVE_SHADER_DIR}/lowered.stamp
        DEPENDS ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
                ${CMAKE_CURRENT_SOURCE_DIR}/../tools/android/lower_int64.py
        VERBATIM)
    add_custom_target(gtav_native_shaders DEPENDS ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h
                                                  ${GTAV_NATIVE_SHADER_DIR}/lowered.stamp)
else()
    add_custom_target(gtav_native_shaders DEPENDS ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h)
endif()
add_dependencies(gtav gtav_native_shaders)
endif()
target_include_directories(gtav PRIVATE ${GTAV_NATIVE_SHADER_DIR})

# Background shader compiler tools (shader_compiler.cpp; build.cmd copies them on Windows):
# <exe>/tools/XenosRecomp, dxc (a wrapper that sets the library path) and shader_common.h.
if(NOT WIN32 AND NOT APPLE AND NOT ANDROID)
    set(GTAV_XR_BIN ${CMAKE_CURRENT_SOURCE_DIR}/../build/xenosrecomp/XenosRecomp/XenosRecomp CACHE FILEPATH
        "XenosRecomp binary for the background shader compiler")
    set(GTAV_TOOLS_DIR $<TARGET_FILE_DIR:gtav>/tools)
    file(WRITE ${CMAKE_BINARY_DIR}/dxc_wrapper.sh
         "#!/bin/sh\nd=$(dirname \"$0\")\nLD_LIBRARY_PATH=\"$d/lib\" exec \"$d/dxc-linux\" \"$@\"\n")
    add_custom_command(TARGET gtav POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GTAV_TOOLS_DIR}/lib
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GTAV_XR_BIN} ${GTAV_TOOLS_DIR}/XenosRecomp
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GTAV_DXC_DIR}/bin/x64/dxc-linux ${GTAV_TOOLS_DIR}/dxc-linux
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${GTAV_DXC_DIR}/lib/x64 ${GTAV_TOOLS_DIR}/lib
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/dxc_wrapper.sh ${GTAV_TOOLS_DIR}/dxc
        COMMAND chmod +x ${GTAV_TOOLS_DIR}/dxc
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/XenosRecomp/XenosRecomp/shader_common.h ${GTAV_TOOLS_DIR}/shader_common.h
        VERBATIM)
elseif(APPLE AND NOT GTAV_IOS)
    # macOS: tools/XenosRecomp and tools/dxc (dxc-macos) find tools/lib/libdxcompiler.dylib through
    # an @executable_path/lib rpath (re-signed ad hoc after install_name_tool).
    set(GTAV_XR_BIN ${CMAKE_CURRENT_SOURCE_DIR}/../build/xenosrecomp-mac/XenosRecomp/XenosRecomp CACHE FILEPATH
        "XenosRecomp binary for the background shader compiler")
    set(GTAV_TOOLS_DIR $<TARGET_FILE_DIR:gtav>/tools)
    add_custom_command(TARGET gtav POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GTAV_TOOLS_DIR}/lib
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GTAV_DXC_DIR}/lib/arm64/libdxcompiler.dylib ${GTAV_TOOLS_DIR}/lib/libdxcompiler.dylib
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/XenosRecomp/XenosRecomp/shader_common.h ${GTAV_TOOLS_DIR}/shader_common.h
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/release/macho_tool.py
                ${GTAV_XR_BIN} ${GTAV_TOOLS_DIR}/XenosRecomp
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/release/macho_tool.py
                ${GTAV_DXC_DIR}/bin/arm64/dxc-macos ${GTAV_TOOLS_DIR}/dxc
        VERBATIM)
endif()

if(GTAV_IOS)
    # Memory log: the Metal device's allocated size (capture.cpp).
    target_link_libraries(gtav PRIVATE "-framework Metal" objc)
    # Setup screen: installs the game data on the phone with the PC/Mac installer's copy stages.
    enable_language(OBJCXX)
    target_sources(gtav PRIVATE src/ios_setup.mm ../loader/src/install.cpp ../loader/src/sources.cpp
                               src/native/adreno_shader_fix.cpp)
    set_source_files_properties(src/ios_setup.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(gtav PRIVATE "-framework UIKit" "-framework UniformTypeIdentifiers")
endif()

# Bug reports name the build (bug_report.cpp).
set_source_files_properties(src/bug_report.cpp PROPERTIES COMPILE_DEFINITIONS "GTAV_BUILD_ID=\"${GTAV_BUILD_ID}\"")
