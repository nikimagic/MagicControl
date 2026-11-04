cmake_minimum_required(VERSION 3.25)
project(HpbsdHardwareProbe LANGUAGES CXX RC)
if(NOT WIN32)
 message(FATAL_ERROR "This explicit diagnostic uses the Windows PawnIO transport")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(MC_ROOT "../../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(MC_LIB "${MC_ROOT}/build/windows/lib/Release")
# Build only after a successful common product build. Reuse exactly its pinned
# embedded runtime and libraries; this target never stages a product release.
add_executable(hpbsd-probe ../src/HpbsdProbeWindows.cpp
 "${MC_ROOT}/src/devices/minisforum/hpbsd/TelemetryWindows.cpp"
 "${MC_ROOT}/src/devices/minisforum/hpbsd/I2EcWindows.cpp"
 "${MC_ROOT}/src/devices/minisforum/hpbsd/FanControlWindows.cpp"
 "${MC_ROOT}/src/platform/windows/pawnio/ApuPowerWindows.cpp"
 "${MC_ROOT}/src/platform/windows/pawnio/PawnTelemetryWindows.cpp"
 "${MC_ROOT}/build/windows/generated/MagicControl/EmbeddedRuntime.rc")
target_compile_features(hpbsd-probe PRIVATE cxx_std_20)
target_compile_definitions(hpbsd-probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(hpbsd-probe PRIVATE /utf-8 /W4)
target_include_directories(hpbsd-probe PRIVATE "${MC_ROOT}/src/core/include" "${MC_ROOT}/src/platform/windows" "${MC_ROOT}/build/windows/generated")
target_link_libraries(hpbsd-probe PRIVATE
 "${MC_LIB}/magiccontrol_platform.lib" "${MC_LIB}/magiccontrol_core.lib"
 "${MC_LIB}/magiccontrol_storage.lib" "${MC_LIB}/magiccontrol_runtime.lib"
 advapi32 ole32 oleaut32 wbemuuid setupapi cfgmgr32 tbs winhttp windowsapp user32 powrprof uuid bcrypt dxgi version wintrust crypt32 shell32)
target_link_options(hpbsd-probe PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
