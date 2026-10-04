cmake_minimum_required(VERSION 3.20)

project(TWINERecompiled VERSION 0.1.74 LANGUAGES C CXX)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/twine-version.txt"
    CONTENT "${PROJECT_VERSION}\n")
execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE TWINE_SOURCE_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT TWINE_SOURCE_REVISION)
    set(TWINE_SOURCE_REVISION "unversioned")
endif()
execute_process(
    COMMAND git -c core.autocrlf=true status --porcelain=v1
        --untracked-files=no
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE TWINE_SOURCE_DIRTY_STATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(TWINE_SOURCE_DIRTY_STATE)
    string(APPEND TWINE_SOURCE_REVISION "+dirty")
endif()
execute_process(
    COMMAND git submodule status --
        lib/N64ModernRuntime
        lib/RecompFrontend
        lib/rt64
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE TWINE_SUBMODULE_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(WIN32)
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
    cmake_policy(SET CMP0135 NEW)
endif()

option(TWINE_TOOLS_ONLY "Build only N64Recomp and RSPRecomp for bootstrap" OFF)
if(TWINE_TOOLS_ONLY)
    add_subdirectory(
        lib/N64ModernRuntime/N64Recomp
        "${CMAKE_BINARY_DIR}/lib/N64ModernRuntime/librecomp/N64Recomp")
    return()
endif()

set(BUILD_TESTING OFF CACHE BOOL "Build dependency tests" FORCE)

set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Build zstd command-line programs" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Build zstd tests" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    set(NFD_PORTAL ON CACHE BOOL "Use the desktop file chooser portal" FORCE)
endif()
add_compile_definitions(HLSL_CPU __PRFCHWINTRIN_H)

add_subdirectory(lib/rt64 "${CMAKE_BINARY_DIR}/rt64")
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(lib/N64ModernRuntime)
target_include_directories(rt64 PRIVATE "${CMAKE_BINARY_DIR}/rt64/src")

if(WIN32)
    include(FetchContent)
    set(SDL2_VERSION "2.30.3")
    FetchContent_Declare(
        sdl2
        URL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip"
        URL_HASH SHA256=89999a6f3dc8f5e4fb03aaea6b4ad01747d9a81690f9f9a0674806e750483a38
    )
    FetchContent_MakeAvailable(sdl2)

    target_link_directories(rt64 INTERFACE "${sdl2_SOURCE_DIR}/lib/x64")
else()
    find_package(SDL2 REQUIRED)
endif()

set(DXC_COMMON_OPTS "-I${PROJECT_SOURCE_DIR}/src")
set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T" "ps_6_0" "-D" "DYNAMIC_RENDER_PARAMS")
set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T" "vs_6_0" "-D" "DYNAMIC_RENDER_PARAMS" "-fvk-invert-y")
set(DXC_CS_OPTS "${DXC_COMMON_OPTS}" "-E" "CSMain" "-T" "cs_6_0")
set(DXC_GS_OPTS "${DXC_COMMON_OPTS}" "-E" "GSMain" "-T" "gs_6_0")
set(DXC_RT_OPTS
    "${DXC_COMMON_OPTS}"
    "-D" "RT_SHADER"
    "-T" "lib_6_3"
    "-fspv-target-env=vulkan1.1spirv1.4"
    "-fspv-extension=SPV_KHR_ray_tracing"
    "-fspv-extension=SPV_EXT_descriptor_indexing")

foreach(dxc_property DXC DXC_EXECUTABLE DXC_DEPENDENCIES DXC_RUNTIME_LIBRARIES RT64_DXC_ROOT)
    get_directory_property(${dxc_property} DIRECTORY lib/rt64 DEFINITION ${dxc_property})
endforeach()
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime")
set(RECOMP_FRONTEND_RT64_PATH "${CMAKE_SOURCE_DIR}/lib/rt64")
add_subdirectory(lib/RecompFrontend)
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(recompui PUBLIC PLUME_SDL_VULKAN_ENABLED)
endif()

set(TWINE_UI_ASSETS
    assets/recomp.rcss
    assets/NotoEmoji-Regular.ttf
    assets/promptfont/promptfont.ttf
    assets/icons/Arrow.svg
    assets/icons/Caret.svg
    assets/icons/Cont.svg
    assets/icons/Keyboard.svg
    assets/icons/Plus.svg
    assets/icons/PlusKeyboard.svg
    assets/icons/Port.svg
    assets/icons/Question.svg
    assets/icons/Quit.svg
    assets/icons/RecordBorder.svg
    assets/icons/RecordSpinner.svg
    assets/icons/Reset.svg
    assets/icons/Trash.svg
    assets/icons/X.svg)
foreach(asset IN LISTS TWINE_UI_ASSETS)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${asset}")
        message(FATAL_ERROR "Required frontend asset is missing: ${asset}")
    endif()
endforeach()

if(MSVC)
    foreach(runtime_target ultramodern librecomp recompui recompinput)
        get_target_property(runtime_options ${runtime_target} COMPILE_OPTIONS)
        list(REMOVE_ITEM runtime_options "-Wno-unused-parameter")
        set_property(TARGET ${runtime_target} PROPERTY COMPILE_OPTIONS "${runtime_options}")
    endforeach()
endif()

set(TWINE_RUNTIME_STAGE_DIR
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}")
set(TWINE_RUNTIME_ASSET_STAMP
    "${TWINE_RUNTIME_STAGE_DIR}/twine_runtime_assets.stamp")
set(TWINE_RUNTIME_ASSET_COMMANDS
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${TWINE_RUNTIME_STAGE_DIR}/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/recomp.rcss"
        "${TWINE_RUNTIME_STAGE_DIR}/assets/recomp.rcss"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/NotoEmoji-Regular.ttf"
        "${TWINE_RUNTIME_STAGE_DIR}/assets/NotoEmoji-Regular.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/icons"
        "${TWINE_RUNTIME_STAGE_DIR}/assets/icons"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/promptfont"
        "${TWINE_RUNTIME_STAGE_DIR}/assets/promptfont")
set(TWINE_RUNTIME_ASSET_DEPENDENCIES ${TWINE_UI_ASSETS})
list(APPEND TWINE_RUNTIME_ASSET_DEPENDENCIES
    "${CMAKE_SOURCE_DIR}/NOTICES"
    "${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/LatoLatin-Regular.ttf"
    "${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/LatoLatin-Bold.ttf")
list(APPEND TWINE_RUNTIME_ASSET_COMMANDS
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/NOTICES"
        "${TWINE_RUNTIME_STAGE_DIR}/NOTICES"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/LatoLatin-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/LatoLatin-Bold.ttf"
        "${TWINE_RUNTIME_STAGE_DIR}/assets")
if(WIN32)

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        message(FATAL_ERROR "MSVC redistributable runtime is unavailable")
    endif()
    list(APPEND TWINE_RUNTIME_ASSET_DEPENDENCIES
        ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
        "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
        "${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/freetype-windows-binaries/release dll/win64/freetype.dll"
        ${DXC_RUNTIME_LIBRARIES}
        "${RT64_DXC_ROOT}/LICENSE-LLVM.txt"
        "${RT64_DXC_ROOT}/LICENSE-MIT.txt"
        "${RT64_DXC_ROOT}/LICENSE-MS.txt")
    list(APPEND TWINE_RUNTIME_ASSET_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/freetype-windows-binaries/release dll/win64/freetype.dll"
            ${DXC_RUNTIME_LIBRARIES}
            "${TWINE_RUNTIME_STAGE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${TWINE_RUNTIME_STAGE_DIR}/licenses/dxc"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${RT64_DXC_ROOT}/LICENSE-LLVM.txt"
            "${RT64_DXC_ROOT}/LICENSE-MIT.txt"
            "${RT64_DXC_ROOT}/LICENSE-MS.txt"
            "${TWINE_RUNTIME_STAGE_DIR}/licenses/dxc")
endif()
add_custom_command(
    OUTPUT "${TWINE_RUNTIME_ASSET_STAMP}"
    ${TWINE_RUNTIME_ASSET_COMMANDS}
    COMMAND "${CMAKE_COMMAND}" -E touch "${TWINE_RUNTIME_ASSET_STAMP}"
    DEPENDS ${TWINE_RUNTIME_ASSET_DEPENDENCIES}
    VERBATIM)
add_custom_target(twine_runtime_assets ALL
    DEPENDS "${TWINE_RUNTIME_ASSET_STAMP}")

set(TWINE_GENERATED_DIR "${CMAKE_SOURCE_DIR}/build/generated")
set(TWINE_GENERATED_CPU_DIR "${TWINE_GENERATED_DIR}/RecompiledFuncs")
set(TWINE_GENERATED_RSP_SOURCE
    "${TWINE_GENERATED_DIR}/rsp/twine_audio.cpp")
set(TWINE_GENERATED_MANIFEST
    "${TWINE_GENERATED_DIR}/generated_sources.cmake")
if(NOT EXISTS "${TWINE_GENERATED_MANIFEST}")
    message(FATAL_ERROR
        "Generated source manifest is missing. Run tools/generate_recomp.py with the validated ROM.")
endif()
include("${TWINE_GENERATED_MANIFEST}")
foreach(generated_source IN LISTS TWINE_GENERATED_CPU_SOURCES)
    if(NOT EXISTS "${generated_source}")
        message(FATAL_ERROR
            "Generated source listed by the manifest is missing: ${generated_source}")
    endif()
endforeach()
if(NOT EXISTS "${TWINE_GENERATED_RSP_SOURCE}")
    message(FATAL_ERROR
        "Generated audio RSP source is missing: ${TWINE_GENERATED_RSP_SOURCE}")
endif()

include(CMake/TwineLocalAot.cmake)

set(TWINE_RUNTIME_SOURCES
    src/keyboard_controls.cpp
    src/local_aot.cpp
    src/rom_metadata.cpp
    src/recomp_overrides.c
    "${TWINE_AOT_DIR}/host_bindings.cpp"
    src/atomic_file.cpp
    src/save_state_file.cpp
    src/save_state_context.cpp
    src/save_state_memory.cpp
    src/save_state_overlays.cpp
    src/save_state_rendezvous.cpp
    src/save_state_service.cpp
    src/save_state_game.cpp
    src/save_state_shortcuts.cpp
    src/audio_host.cpp
    src/audio_settings.cpp
    src/audio_status_ui.cpp
    src/resume_recovery.cpp
    src/steam_deck_grips.cpp
    src/twine_qol.cpp
    src/modern_grapple.cpp
    src/world_render_context.cpp
    src/native_queries.cpp
    src/native_render_tags.cpp src/native_visibility.cpp
    src/native_ui_render.cpp
    src/pause_background.cpp
    src/font_reconstruction.cpp
    src/texture_reconstruction.cpp
    src/texture_material.cpp
    src/texture_native.cpp
    src/texture_bank.cpp src/material_query.cpp
    src/texture_pack.cpp
    src/texture_installation.cpp
    src/modern_textures.cpp
    src/font_textures.cpp
    src/frontend_text.cpp
    src/simulation_clock.cpp
    src/hud_crosshair.cpp
    src/native_aim.cpp
    src/objectives_data.cpp
    src/objectives_overlay.cpp
    src/vision_battery_data.cpp
    src/vision_battery.cpp
    src/radial_menu.cpp
    src/radial_view.cpp
    src/guided_weapon.cpp
    src/civilian_health.cpp
    src/npc_protection.cpp
    src/sprint.cpp
    src/health_regeneration.cpp
    src/weapon_tuning.cpp
    src/weapon_modes.cpp
    src/player_jump.cpp
    src/fall_damage.cpp
    src/oxygen.cpp
    src/campaign_profile.cpp
    src/debrief_menu.cpp
    src/input_prompts.cpp
    src/night_vision_overlay.cpp
    src/native_options.cpp
    src/cheats.cpp
    src/register_overlays.cpp
    src/controller_pak.cpp
    src/pfs_wrappers.cpp)

add_library(twine_runtime_objects OBJECT ${TWINE_RUNTIME_SOURCES})
string(SHA256 TWINE_STATE_SOURCE_FINGERPRINT "${TWINE_SOURCE_REVISION}\n${TWINE_SUBMODULE_STATUS}")
target_compile_definitions(twine_runtime_objects PRIVATE
    TWINE_SOURCE_FINGERPRINT="${TWINE_STATE_SOURCE_FINGERPRINT}")
target_include_directories(twine_runtime_objects PRIVATE
    include
    "${TWINE_AOT_DIR}"
    lib/N64ModernRuntime/N64Recomp/include
    lib/N64ModernRuntime/librecomp/include
    lib/N64ModernRuntime/ultramodern/include
    lib/rt64/src/contrib
    lib/rt64/src/contrib/hlslpp/include
    lib/rt64/src/contrib/dxc/inc
    lib/rt64/src
    lib/rt64/src/rhi
    lib/rt64/src/render
    lib/rt64/src/contrib/nativefiledialog-extended/src/include
    lib/RecompFrontend/recompinput/include
    lib/RecompFrontend/recompui/include
    lib/RecompFrontend/recompui/src)
target_link_libraries(twine_runtime_objects PRIVATE
    recompui
    recompinput
    ultramodern
    rt64)
if(WIN32)
    target_include_directories(twine_runtime_objects PRIVATE
        "${sdl2_SOURCE_DIR}/include")
else()
    target_link_libraries(twine_runtime_objects PRIVATE SDL2::SDL2)
endif()

if(MSVC)
    target_compile_options(twine_runtime_objects PRIVATE /bigobj /fp:strict)
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(twine_runtime_objects PRIVATE -msse4.1)
    endif()
endif()

function(twine_configure_executable target)
    add_executable(${target}
        src/main.cpp
        "${TWINE_AOT_RESOURCE}"
        $<TARGET_OBJECTS:twine_runtime_objects>)
    target_include_directories(${target} PRIVATE
        include
        "${TWINE_AOT_DIR}"
        lib/N64ModernRuntime/N64Recomp/include
        lib/N64ModernRuntime/librecomp/include
        lib/N64ModernRuntime/ultramodern/include
        lib/rt64/src/contrib
        lib/rt64/src/contrib/hlslpp/include
        lib/rt64/src/contrib/dxc/inc
        lib/rt64/src
        lib/rt64/src/rhi
        lib/rt64/src/render
        lib/rt64/src/contrib/nativefiledialog-extended/src/include
        lib/RecompFrontend/recompinput/include
        lib/RecompFrontend/recompui/include
        lib/RecompFrontend/recompui/src)
    target_compile_definitions(${target} PRIVATE
        TWINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        TWINE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        TWINE_VERSION_PATCH=${PROJECT_VERSION_PATCH})
    if(MSVC)
        target_compile_options(${target} PRIVATE /bigobj /fp:strict)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(${target} PRIVATE -msse4.1)
    endif()
    target_link_libraries(${target} PRIVATE
        recompui
        recompinput
        librecomp
        ultramodern
        rt64
        nfd)
    add_dependencies(${target} twine_runtime_assets)
    if(WIN32)
        target_include_directories(${target} PRIVATE "${sdl2_SOURCE_DIR}/include")
        target_link_directories(${target} PRIVATE "${sdl2_SOURCE_DIR}/lib/x64")
        target_link_libraries(${target} PRIVATE SDL2 Winmm.lib)
    else()
        target_link_libraries(${target} PRIVATE SDL2::SDL2 ${CMAKE_DL_LIBS})
        target_link_options(${target} PRIVATE "-Wl,-z,noexecstack")
    endif()
    set_property(TARGET ${target} PROPERTY
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endfunction()

twine_configure_executable(TWINERecompiled)
