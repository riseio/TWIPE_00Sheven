find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(TWINE_AOT_DIR "${CMAKE_SOURCE_DIR}/build/generated/local-aot/${CMAKE_SYSTEM_NAME}")
if(WIN32)
    set(aot_platform windows)
    set(aot_suffix .exe)
    set(aot_archive_name zig-x86_64-windows-0.14.1.zip)
    set(aot_archive_hash 554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c)
    set(aot_generators "${CMAKE_BINARY_DIR}/Release")
else()
    set(aot_platform linux)
    set(aot_suffix "")
    set(aot_archive_name zig-x86_64-linux-0.14.1.tar.xz)
    set(aot_archive_hash 24aeeec8af16c381934a6cd7d95c807a8cb2cf7df9fa40d359aa884195c4716c)
    set(aot_generators "${CMAKE_BINARY_DIR}")
endif()
set(aot_cache "${CMAKE_SOURCE_DIR}/build/tools/local-aot/${aot_platform}")
file(MAKE_DIRECTORY "${aot_cache}")
set(aot_archive "${aot_cache}/${aot_archive_name}")
set(actual_hash "")
if(EXISTS "${aot_archive}")
    file(SHA256 "${aot_archive}" actual_hash)
endif()
if(NOT actual_hash STREQUAL aot_archive_hash)
    file(DOWNLOAD "https://ziglang.org/download/0.14.1/${aot_archive_name}"
        "${aot_archive}.pending" EXPECTED_HASH "SHA256=${aot_archive_hash}" TLS_VERIFY ON SHOW_PROGRESS)
    file(RENAME "${aot_archive}.pending" "${aot_archive}")
endif()
set(aot_compiler "${aot_cache}/zig-x86_64-${aot_platform}-0.14.1")
if(NOT EXISTS "${aot_compiler}/zig${aot_suffix}")
    file(ARCHIVE_EXTRACT INPUT "${aot_archive}" DESTINATION "${aot_cache}")
endif()
set(aot_inputs
    "${CMAKE_SOURCE_DIR}/tools/prepare_local_aot.py"
    "${CMAKE_SOURCE_DIR}/CMake/TwineLocalAot.cmake"
    "${CMAKE_SOURCE_DIR}/include/local_aot_recipe.hpp"
    "${CMAKE_SOURCE_DIR}/include/local_aot_abi.h"
    "${CMAKE_SOURCE_DIR}/include/twine_recomp.h"
    "${CMAKE_SOURCE_DIR}/config/twine.us.rev0.toml"
    "${CMAKE_SOURCE_DIR}/config/twine.us.rev0.syms.toml"
    "${CMAKE_SOURCE_DIR}/config/twine.audio.us.rev0.toml"
    "${TWINE_GENERATED_CPU_DIR}/funcs.h"
    ${TWINE_GENERATED_CPU_SOURCES}
    "${aot_generators}/N64Recomp${aot_suffix}"
    "${aot_generators}/RSPRecomp${aot_suffix}")
file(GLOB aot_headers CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include/librecomp/rsp*.hpp")
list(APPEND aot_inputs ${aot_headers}
    "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include/recomp.h"
    "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include/librecomp/sections.h"
    "${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include/ultramodern/ultra64.h")
set(aot_identity "${aot_archive_hash}\n")
foreach(input IN LISTS aot_inputs)
    file(SHA256 "${input}" input_hash)
    string(APPEND aot_identity "${input_hash}\n")
endforeach()
string(SHA256 aot_identity "${aot_identity}")
set(aot_stamp "${TWINE_AOT_DIR}/inputs.sha256")
set(prior_identity "")
if(EXISTS "${aot_stamp}")
    file(READ "${aot_stamp}" prior_identity)
endif()
if(NOT prior_identity STREQUAL aot_identity OR NOT EXISTS "${TWINE_AOT_DIR}/bundle.zip")
    set(aot_dll_arguments "")
    foreach(dll IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        list(APPEND aot_dll_arguments --runtime-library "${dll}")
    endforeach()
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/prepare_local_aot.py"
        --root "${CMAKE_SOURCE_DIR}" --output "${TWINE_AOT_DIR}" --compiler "${aot_compiler}"
        --cpu-generator "${aot_generators}/N64Recomp${aot_suffix}"
        --rsp-generator "${aot_generators}/RSPRecomp${aot_suffix}" ${aot_dll_arguments}
        COMMAND_ERROR_IS_FATAL ANY)
    file(WRITE "${aot_stamp}" "${aot_identity}")
endif()
set(aot_configure_inputs ${aot_inputs})
list(REMOVE_ITEM aot_configure_inputs
    "${aot_generators}/N64Recomp${aot_suffix}" "${aot_generators}/RSPRecomp${aot_suffix}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${aot_configure_inputs})
if(WIN32)
    file(WRITE "${TWINE_AOT_DIR}/bundle.rc" "104 RCDATA \"${TWINE_AOT_DIR}/bundle.zip\"\n")
    set(TWINE_AOT_RESOURCE "${TWINE_AOT_DIR}/bundle.rc")
    set_source_files_properties("${TWINE_AOT_RESOURCE}" PROPERTIES OBJECT_DEPENDS "${TWINE_AOT_DIR}/bundle.zip")
else()
    add_custom_command(OUTPUT "${TWINE_AOT_DIR}/bundle.o"
        COMMAND "${CMAKE_LINKER}" -r -b binary bundle.zip -o bundle.raw.o
        COMMAND "${CMAKE_OBJCOPY}" --rename-section .data=.rodata,alloc,load,readonly,data,contents bundle.raw.o bundle.o
        WORKING_DIRECTORY "${TWINE_AOT_DIR}" DEPENDS "${TWINE_AOT_DIR}/bundle.zip" VERBATIM)
    set(TWINE_AOT_RESOURCE "${TWINE_AOT_DIR}/bundle.o")
endif()
