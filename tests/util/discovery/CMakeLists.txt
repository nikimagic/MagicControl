cmake_minimum_required(VERSION 3.25)
project(FirmwareDiscovery LANGUAGES CXX RC)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(MC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(MC_LIB "${MC_ROOT}/build/windows/lib/Release")
add_executable(firmware-discovery ../src/FirmwareDiscoveryWindows.cpp)
target_compile_features(firmware-discovery PRIVATE cxx_std_20)
target_compile_definitions(firmware-discovery PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(firmware-discovery PRIVATE /utf-8 /W4)
target_include_directories(firmware-discovery PRIVATE "${MC_ROOT}/src/core/include" "${MC_ROOT}/src/platform/windows" "${MC_ROOT}/build/windows/generated")
# Reuse the existing read-only backend, mutex, identity/driver evidence and JSON.
# Do not build or modify the product while the owner's source edits are pending.
target_link_libraries(firmware-discovery PRIVATE
 "${MC_LIB}/magiccontrol_platform.lib" "${MC_LIB}/magiccontrol_core.lib"
 "${MC_LIB}/magiccontrol_storage.lib" "${MC_LIB}/magiccontrol_runtime.lib"
 advapi32 ole32 oleaut32 wbemuuid setupapi cfgmgr32 tbs winhttp windowsapp user32 powrprof uuid bcrypt dxgi version wintrust crypt32 shell32)
target_link_options(firmware-discovery PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
set_target_properties(firmware-discovery PROPERTIES OUTPUT_NAME FirmwareDiscovery)
add_executable(hpbsd-probe ../src/HpbsdProbeWindows.cpp "${MC_ROOT}/src/devices/minisforum/hpbsd/TelemetryWindows.cpp")
target_sources(hpbsd-probe PRIVATE "${MC_ROOT}/src/platform/windows/pawnio/ApuPowerWindows.cpp")
target_sources(hpbsd-probe PRIVATE "${MC_ROOT}/src/devices/minisforum/hpbsd/I2EcWindows.cpp" "${MC_ROOT}/src/devices/minisforum/hpbsd/FanControlWindows.cpp")
target_sources(hpbsd-probe PRIVATE "${MC_ROOT}/build/windows/generated/MagicControl/EmbeddedRuntime.rc")
target_compile_features(hpbsd-probe PRIVATE cxx_std_20)
target_compile_definitions(hpbsd-probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(hpbsd-probe PRIVATE /utf-8 /W4)
target_include_directories(hpbsd-probe PRIVATE "${MC_ROOT}/src/core/include" "${MC_ROOT}/src/platform/windows" "${MC_ROOT}/build/windows/generated")
target_link_libraries(hpbsd-probe PRIVATE
 "${MC_LIB}/magiccontrol_platform.lib" "${MC_LIB}/magiccontrol_core.lib"
 "${MC_LIB}/magiccontrol_storage.lib" "${MC_LIB}/magiccontrol_runtime.lib"
 advapi32 ole32 oleaut32 wbemuuid setupapi cfgmgr32 tbs winhttp windowsapp user32 powrprof uuid bcrypt dxgi version wintrust crypt32 shell32)
target_link_options(hpbsd-probe PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
