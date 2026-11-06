cmake_minimum_required(VERSION 3.25)

project(MagicControl
    VERSION 0.1.0
    DESCRIPTION "MagicControl - cross-platform laptop control (fans, lighting, power)"
    LANGUAGES CXX)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Embed the MSVC CRT in our Windows binaries. The portable package then needs
# no msvcp/vcruntime/concrt DLLs beside MagicControl.exe.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(MAGICCONTROL_SIGN "Sign Windows PE binaries after every build" OFF)
option(MAGICCONTROL_BUILD_GUI   "Build the graphical interface"     ON)
option(MAGICCONTROL_BUILD_CLI   "Build the SSH command line interface" ON)
option(MAGICCONTROL_BUILD_TESTS "Build unit tests"                  ON)
option(MAGICCONTROL_WERROR      "Treat warnings as errors"          OFF)

# Every Windows PE target gets its own post-build Authenticode step. Keeping the
# hook here means CLI, tests, GUI and future DLL targets cannot silently diverge.
function(magiccontrol_sign_target target)
    if(WIN32 AND MAGICCONTROL_SIGN)
        find_program(MAGICCONTROL_POWERSHELL NAMES pwsh powershell REQUIRED)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${MAGICCONTROL_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_SOURCE_DIR}/Tools/Setup-Signing.ps1"
                    -SkipTrust
                    -BinaryDirectory "$<TARGET_FILE_DIR:${target}>"
                    -BinaryPath "$<TARGET_FILE:${target}>"
                    -ManifestPath "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>-signing-manifest.json"
            COMMENT "Signing $<TARGET_FILE_NAME:${target}>"
            VERBATIM)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /MP)
    if(MAGICCONTROL_WERROR)
        add_compile_options(/WX)
    endif()
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(MAGICCONTROL_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Generated version header
# ---------------------------------------------------------------------------
if(EXISTS "${PROJECT_SOURCE_DIR}/build/product-version.txt")
    file(READ "${PROJECT_SOURCE_DIR}/build/product-version.txt" _product_version)
    string(STRIP "${_product_version}" _product_version)
    if(NOT _product_version MATCHES "^0\\.1\\.([0-9]+)$")
        message(FATAL_ERROR "Invalid selected product version: ${_product_version}")
    endif()
    set(PROJECT_VERSION "${_product_version}")
    set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_1}")
endif()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/MagicControl/Version.h"
    @ONLY)

# Explicit GUI/CLI targets consume the SAME selected product version. Rebuilding
# either OS must not allocate another number or diverge from the other platform.
add_custom_target(magiccontrol-build-version
    COMMAND "${CMAKE_COMMAND}"
        "-DCOUNTER_DIRECTORY=${PROJECT_SOURCE_DIR}/build"
        "-DVERSION_TEMPLATE=${PROJECT_SOURCE_DIR}/cmake/Version.h.in"
        "-DVERSION_HEADER=${CMAKE_CURRENT_BINARY_DIR}/generated/MagicControl/Version.h"
        "-DPROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
        "-DPROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
        "-DPROJECT_NAME=${PROJECT_NAME}"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        -P "${PROJECT_SOURCE_DIR}/cmake/IncrementBuildVersion.cmake"
    VERBATIM)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_dependencies(magiccontrol_core magiccontrol-build-version)
add_subdirectory(src/devices)
add_subdirectory(src/protocols)
add_subdirectory(src/gui/common)

if(WIN32)
    enable_language(RC)
    add_subdirectory(src/platform/windows)
elseif(UNIX)
    # The Linux backend is written on Kubuntu 26.04 against real hardware.
    # Until it exists, only the platform independent core and its tests build.
    add_subdirectory(src/platform/linux)
endif()

add_subdirectory(tests/util)
if(MAGICCONTROL_BUILD_CLI)
    add_subdirectory(src/cli)
    add_dependencies(magiccontrol magiccontrol-build-version)
endif()

if(MAGICCONTROL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MAGICCONTROL_BUILD_GUI)
    add_subdirectory(src/update)
    if(WIN32)
        add_subdirectory(src/gui/windows)
    elseif(UNIX)
        add_subdirectory(src/gui/linux)
    else()
        message(WARNING "No graphical frontend is available for this platform")
    endif()
endif()

# Source files and non-PE artifacts cannot carry Authenticode signatures. The
# manifest gives every tracked source/configuration/resource a SHA-256 identity,
# while the PE files above also carry an actual publisher signature.
set(MAGICCONTROL_ARTIFACT_DEPENDENCIES)
foreach(_target magiccontrol_tests magiccontrol-gui magiccontrol)
    if(TARGET ${_target})
        list(APPEND MAGICCONTROL_ARTIFACT_DEPENDENCIES ${_target})
    endif()
endforeach()
if(TARGET magiccontrol-gui)
    set(MAGICCONTROL_ARTIFACT_BINARY_DIR "$<TARGET_FILE_DIR:magiccontrol-gui>")
else()
    set(MAGICCONTROL_ARTIFACT_BINARY_DIR "${CMAKE_BINARY_DIR}/bin")
endif()
set(MAGICCONTROL_ARTIFACT_OUTPUT "${MAGICCONTROL_ARTIFACT_BINARY_DIR}/artifact-manifest.json")
add_custom_target(magiccontrol-artifact-manifest ALL
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_DIR=${MAGICCONTROL_ARTIFACT_BINARY_DIR}
            -DOUTPUT=${MAGICCONTROL_ARTIFACT_OUTPUT}
            -P "${CMAKE_SOURCE_DIR}/cmake/ArtifactManifest.cmake"
    DEPENDS ${MAGICCONTROL_ARTIFACT_DEPENDENCIES}
    VERBATIM)

if(UNIX AND TARGET magiccontrol-gui)
    set(_cli_stage_args)
    set(_cli_stage_dependencies)
    if(TARGET magiccontrol)
        list(APPEND _cli_stage_args "-DCLI_EXECUTABLE=$<TARGET_FILE:magiccontrol>")
        list(APPEND _cli_stage_dependencies magiccontrol)
    endif()
    set(MAGICCONTROL_PORTABLE_LINUX_DIR "${PROJECT_SOURCE_DIR}/MagicControl/linux")
    add_custom_target(magiccontrol-portable-linux ALL
        COMMAND ${CMAKE_COMMAND}
                "-DCONFIG=$<CONFIG>"
                "-DGUI_EXECUTABLE=$<TARGET_FILE:magiccontrol-gui>"
                ${_cli_stage_args}
                "-DVERSION_FILE=${CMAKE_BINARY_DIR}/generated/MagicControl/Version.h.version"
                "-DLAYER_LIBRARY=$<TARGET_PROPERTY:magiccontrol-gui,MAGICCONTROL_OSD_LAYER_RUNTIME>"
                "-DDESTINATION=${MAGICCONTROL_PORTABLE_LINUX_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/StagePortableLinux.cmake"
        DEPENDS magiccontrol-gui ${_cli_stage_dependencies} magiccontrol-artifact-manifest
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Optional code signing
# ---------------------------------------------------------------------------
# Signing is part of the build, not a step to remember afterwards. It is attached
# to the GUI target in src/gui/windows/CMakeLists.txt, because
# add_custom_command(TARGET ...) requires the target to exist already.
#
# Every build rewrites the executables and erases their signatures. An unsigned
# binary makes Windows report "Unknown publisher" and refuse to start it through
# SmartScreen, which looks exactly like a broken application. Signing was
# originally a separate manual command, so this happened repeatedly.
#
# MAGICCONTROL_SIGN=OFF disables it, for example on a machine with no certificate.


# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== MagicControl ${PROJECT_VERSION} ===")
message(STATUS "  System      : ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Compiler    : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  GUI         : ${MAGICCONTROL_BUILD_GUI}")
message(STATUS "  CLI         : ${MAGICCONTROL_BUILD_CLI}")
message(STATUS "  Tests       : ${MAGICCONTROL_BUILD_TESTS}")
message(STATUS "  Signing     : ${MAGICCONTROL_SIGN}")
message(STATUS "")
