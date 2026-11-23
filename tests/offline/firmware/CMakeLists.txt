cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MagicControlFirmwareOfflineTests LANGUAGES CXX)
    enable_testing()
endif()
get_filename_component(MC_REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

# Firmware capture pauses background sampling through the common core. These
# fixtures link the actual gate/worker/BIOS entry but fake platform collectors:
# no device, native firmware utility or driver is loaded by the tests.
add_executable(firmware_telemetry_gate_offline_tests test_telemetry_gate.cpp)
add_executable(firmware_telemetry_worker_offline_tests test_telemetry_worker.cpp)
add_executable(firmware_gpu_presence_offline_tests test_gpu_presence.cpp)
add_executable(firmware_async_reaper_offline_tests test_async_reaper.cpp)
add_executable(firmware_event_time_offline_tests test_firmware_event_time.cpp)
add_executable(firmware_deferred_telemetry_offline_tests test_deferred_telemetry.cpp)
add_executable(firmware_rotation_guard_offline_tests test_display_rotation_guard.cpp "${MC_REPO}/src/core/src/ActionLog.cpp")
add_executable(firmware_linux_settings_guard_offline_tests test_linux_settings_guard.cpp)
target_include_directories(firmware_linux_settings_guard_offline_tests PRIVATE "${MC_REPO}/src/platform/linux")
add_executable(firmware_bios_pause_offline_tests test_bios_pause_collector.cpp
    "${MC_REPO}/src/platform/common/BiosRomDump.cpp"
    "${MC_REPO}/src/platform/common/InfoDumpResource.cpp")
file(GLOB_RECURSE MC_FIRMWARE_PROFILES CONFIGURE_DEPENDS "${MC_REPO}/src/devices/*/Profile.cpp")
add_executable(firmware_keys_telemetry_offline_tests test_keys_telemetry.cpp ${MC_FIRMWARE_PROFILES}
    "${MC_REPO}/src/devices/DeviceRegistry.cpp"
    "${MC_REPO}/src/devices/mechrevo/ReportedProfiles.cpp"
    "${MC_REPO}/src/core/src/KeysLifecycle.cpp"
    "${MC_REPO}/src/core/src/ActionLog.cpp")
add_executable(firmware_gpu_telemetry_offline_tests test_gpu_telemetry.cpp
    "${MC_REPO}/src/platform/common/DiscreteGpuOperation.cpp"
    "${MC_REPO}/src/core/src/ActionLog.cpp")
target_include_directories(firmware_gpu_telemetry_offline_tests PRIVATE "${MC_REPO}/src/platform/common")
foreach(target firmware_telemetry_gate_offline_tests firmware_telemetry_worker_offline_tests firmware_gpu_presence_offline_tests firmware_async_reaper_offline_tests firmware_event_time_offline_tests firmware_deferred_telemetry_offline_tests firmware_rotation_guard_offline_tests firmware_linux_settings_guard_offline_tests firmware_bios_pause_offline_tests firmware_keys_telemetry_offline_tests firmware_gpu_telemetry_offline_tests)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE "${MC_REPO}/src/core/include")
    target_link_libraries(${target} PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W4)
    endif()
endforeach()
add_test(NAME firmware_telemetry_gate COMMAND firmware_telemetry_gate_offline_tests)
add_test(NAME firmware_telemetry_worker COMMAND firmware_telemetry_worker_offline_tests)
add_test(NAME firmware_gpu_presence COMMAND firmware_gpu_presence_offline_tests)
add_test(NAME firmware_async_reaper COMMAND firmware_async_reaper_offline_tests)
add_test(NAME firmware_event_time COMMAND firmware_event_time_offline_tests)
add_test(NAME firmware_deferred_telemetry COMMAND firmware_deferred_telemetry_offline_tests)
add_test(NAME firmware_rotation_guard COMMAND firmware_rotation_guard_offline_tests)
add_test(NAME firmware_linux_settings_guard COMMAND firmware_linux_settings_guard_offline_tests)
add_test(NAME firmware_bios_pause COMMAND firmware_bios_pause_offline_tests)
add_test(NAME firmware_keys_telemetry COMMAND firmware_keys_telemetry_offline_tests)
add_test(NAME firmware_gpu_telemetry COMMAND firmware_gpu_telemetry_offline_tests)
set_tests_properties(firmware_telemetry_gate firmware_telemetry_worker firmware_gpu_presence firmware_async_reaper firmware_event_time firmware_deferred_telemetry firmware_rotation_guard firmware_linux_settings_guard firmware_bios_pause firmware_keys_telemetry firmware_gpu_telemetry
    PROPERTIES LABELS "offline;firmware;telemetry" TIMEOUT 25)
if(UNIX)
    # Actual Linux FANs owner, with a fake MonitoringReader and in-memory source.
    # Link-time section GC excludes unused settings/file paths from the fixture.
    add_executable(firmware_linux_fans_telemetry_offline_tests test_linux_fans_telemetry.cpp ${MC_FIRMWARE_PROFILES}
        "${MC_REPO}/src/devices/DeviceRegistry.cpp" "${MC_REPO}/src/devices/mechrevo/ReportedProfiles.cpp"
        "${MC_REPO}/src/core/src/ActionLog.cpp" "${MC_REPO}/src/core/src/PortableSettings.cpp"
        "${MC_REPO}/src/core/src/ResourceCatalog.cpp" "${MC_REPO}/src/platform/linux/FansServicesLinux.cpp")
    target_compile_features(firmware_linux_fans_telemetry_offline_tests PRIVATE cxx_std_20)
    target_include_directories(firmware_linux_fans_telemetry_offline_tests PRIVATE "${MC_REPO}/src/core/include" "${MC_REPO}/src/platform/linux")
    target_compile_options(firmware_linux_fans_telemetry_offline_tests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(firmware_linux_fans_telemetry_offline_tests PRIVATE -Wl,--gc-sections)
    target_link_libraries(firmware_linux_fans_telemetry_offline_tests PRIVATE Threads::Threads)
    add_test(NAME firmware_linux_fans_telemetry COMMAND firmware_linux_fans_telemetry_offline_tests)
    set_tests_properties(firmware_linux_fans_telemetry PROPERTIES LABELS "offline;firmware;telemetry;fans" TIMEOUT 25)
endif()
if(WIN32)
    add_executable(firmware_wmi_event_time_windows_offline_tests test_wmi_event_time_windows.cpp)
    target_compile_features(firmware_wmi_event_time_windows_offline_tests PRIVATE cxx_std_20)
    target_include_directories(firmware_wmi_event_time_windows_offline_tests PRIVATE "${MC_REPO}/src/core/include" "${MC_REPO}/src/platform/windows")
    target_link_libraries(firmware_wmi_event_time_windows_offline_tests PRIVATE oleaut32)
    target_compile_options(firmware_wmi_event_time_windows_offline_tests PRIVATE /utf-8 /W4)
    add_test(NAME firmware_wmi_event_time_windows COMMAND firmware_wmi_event_time_windows_offline_tests)
    set_tests_properties(firmware_wmi_event_time_windows PROPERTIES LABELS "offline;firmware;telemetry" TIMEOUT 10)
endif()
add_executable(firmware_validator_offline_tests test_validators.cpp)
add_executable(insyde_reader_offline_tests test_insyde.cpp)
add_executable(operation_messages_offline_tests test_operation_messages.cpp)
target_compile_features(operation_messages_offline_tests PRIVATE cxx_std_20)
target_include_directories(operation_messages_offline_tests PRIVATE "${MC_REPO}/src/core/include" "${MC_REPO}/src/gui/common")
if(MSVC)
    target_compile_options(operation_messages_offline_tests PRIVATE /utf-8 /W4)
endif()
add_test(NAME firmware_reader_transcript COMMAND operation_messages_offline_tests)
target_compile_features(insyde_reader_offline_tests PRIVATE cxx_std_20)
target_include_directories(insyde_reader_offline_tests PRIVATE "${MC_REPO}/src/platform/common")
if(MSVC)
    target_compile_options(insyde_reader_offline_tests PRIVATE /utf-8 /W4)
endif()
target_compile_features(firmware_validator_offline_tests PRIVATE cxx_std_20)
target_include_directories(firmware_validator_offline_tests PRIVATE "${MC_REPO}/src/platform/common" "${MC_REPO}/tests/offline")
if(MSVC)
    target_compile_options(firmware_validator_offline_tests PRIVATE /utf-8 /W4)
endif()
foreach(suite afu fpt16 fpt19 nvidia)
    add_test(NAME firmware_${suite}_synthetic COMMAND firmware_validator_offline_tests ${suite})
    set_tests_properties(firmware_${suite}_synthetic PROPERTIES LABELS "offline;firmware;${suite}" TIMEOUT 90)
endforeach()
add_test(NAME firmware_real_files COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_real_files.py"
    --reader "$<TARGET_FILE:firmware_validator_offline_tests>" --repo "${MC_REPO}" --scratch "${CMAKE_CURRENT_BINARY_DIR}/real-fixtures")
set_tests_properties(firmware_real_files PROPERTIES LABELS "offline;firmware;private-fixtures" SKIP_RETURN_CODE 77 TIMEOUT 120)
# No platform, core, GUI, NVML, driver, vendor payload or hardware API linkage.

# These fixtures live with the tracked tests: a fresh source checkout must not
# depend on an ignored research directory or link the native NVFlash adapter.
add_executable(nvflash_contract_offline_tests test_nvflash_contract.cpp)
add_executable(nvflash_state_offline_tests test_nvflash_state.cpp)
add_executable(nvidia_collector_offline_tests test_nvflash_collector.cpp
    "${MC_REPO}/src/platform/common/VbiosRomDump.cpp" "${MC_REPO}/src/platform/common/InfoDumpResource.cpp")
target_sources(nvidia_collector_offline_tests PRIVATE nvidia_logging_stub.cpp nvidia_info_stub.cpp)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/MagicControl")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/MagicControl/Version.h" "#pragma once\n#define MAGICCONTROL_VERSION_STRING \"offline-firmware-tests\"\n")
foreach(target nvflash_contract_offline_tests nvidia_collector_offline_tests nvflash_state_offline_tests)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE "${MC_REPO}/src/platform/common" "${MC_REPO}/src/core/include" "${CMAKE_CURRENT_BINARY_DIR}/generated")
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endforeach()
if(WIN32)
    target_link_libraries(nvidia_collector_offline_tests PRIVATE bcrypt)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NV_TEST_GLIB REQUIRED IMPORTED_TARGET glib-2.0)
    target_link_libraries(nvidia_collector_offline_tests PRIVATE PkgConfig::NV_TEST_GLIB)
endif()
add_test(NAME firmware_nvidia_contract COMMAND nvflash_contract_offline_tests)
add_test(NAME firmware_nvidia_state COMMAND nvflash_state_offline_tests)
add_test(NAME firmware_nvidia_collector COMMAND nvidia_collector_offline_tests "${CMAKE_CURRENT_BINARY_DIR}/nvidia-captures")
add_test(NAME firmware_nvidia_gm6_saved_rom
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_LIST_DIR}/test_gm6_vbios_file.py"
    "$<TARGET_FILE:firmware_validator_offline_tests>" "${MC_REPO}/../BIOS_EC/GM6IR0C/VBIOS_Юрия_20261106_обычный.rom")
set_tests_properties(firmware_nvidia_gm6_saved_rom PROPERTIES LABELS "offline;firmware;nvidia;private-fixtures" SKIP_RETURN_CODE 77 TIMEOUT 30)
set_tests_properties(firmware_nvidia_contract firmware_nvidia_collector firmware_nvidia_state PROPERTIES LABELS "offline;firmware;nvidia" TIMEOUT 90)
if(WIN32)
    add_executable(nvflash_lease_windows test_nvflash_lease_windows.cpp)
    target_compile_features(nvflash_lease_windows PRIVATE cxx_std_20)
    set_property(TARGET nvflash_lease_windows PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
    target_compile_options(nvflash_lease_windows PRIVATE /utf-8)
    target_include_directories(nvflash_lease_windows PRIVATE "${MC_REPO}/src/platform/common")
    set(MC_LIB "${MC_REPO}/build/windows/lib/Release")
    target_link_libraries(nvflash_lease_windows PRIVATE
        "${MC_LIB}/magiccontrol_platform.lib" "${MC_LIB}/magiccontrol_core.lib"
        "${MC_LIB}/magiccontrol_storage.lib" "${MC_LIB}/magiccontrol_runtime.lib"
        advapi32 ole32 oleaut32 wbemuuid setupapi cfgmgr32 tbs winhttp windowsapp
        user32 powrprof uuid bcrypt dxgi version wintrust crypt32 shell32)
    add_test(NAME firmware_gpu_capture_coordination COMMAND nvflash_lease_windows)
endif()
