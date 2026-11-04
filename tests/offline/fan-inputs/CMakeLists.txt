cmake_minimum_required(VERSION 3.25)
project(FanInputOfflineChecks LANGUAGES CXX)
enable_testing()
if(NOT WIN32)
 message(FATAL_ERROR "Uses the existing Windows controller with a simulated EC/environment only")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(MC_ROOT "../../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(MC_LIB "${MC_ROOT}/build/windows/lib/Release")
add_executable(fan-inputs-offline "${MC_ROOT}/tests/test_ec_manual_windows.cpp")
target_compile_features(fan-inputs-offline PRIVATE cxx_std_20)
target_compile_definitions(fan-inputs-offline PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(fan-inputs-offline PRIVATE /utf-8 /W4)
target_include_directories(fan-inputs-offline PRIVATE "${MC_ROOT}/src/core/include" "${MC_ROOT}/build/windows/generated")
# Reuse the current product code; the test explicitly injects Memory/Environment
# and never creates an actual EC transport, hardware owner or sensor provider.
target_link_libraries(fan-inputs-offline PRIVATE
 "${MC_LIB}/magiccontrol_platform.lib" "${MC_LIB}/magiccontrol_core.lib"
 "${MC_LIB}/magiccontrol_storage.lib" "${MC_LIB}/magiccontrol_runtime.lib"
 advapi32 ole32 oleaut32 wbemuuid setupapi cfgmgr32 tbs winhttp windowsapp
 user32 powrprof uuid bcrypt dxgi version wintrust crypt32 shell32)
add_test(NAME fan_input_bounds_and_codec COMMAND fan-inputs-offline --codec-only)
