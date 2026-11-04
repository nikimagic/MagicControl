# ---------------------------------------------------------------------------
# Unit tests.
#
# These cover the logic that does not need hardware: register decoding, packet
# layout, error formatting and the board identity comparison. Hardware paths are
# exercised by the CLI on a real laptop instead.
# ---------------------------------------------------------------------------
add_executable(magiccontrol_battery_aging_tests test_battery_aging.cpp test_board_fixture.cpp)
target_link_libraries(magiccontrol_battery_aging_tests PRIVATE magiccontrol_core)
add_test(NAME magiccontrol_battery_aging_tests COMMAND magiccontrol_battery_aging_tests)
add_executable(magiccontrol_battery_aging_ui_tests test_battery_aging_ui.cpp)
if(UNIX)
    target_link_libraries(magiccontrol_battery_aging_ui_tests PRIVATE "$<LINK_GROUP:RESCAN,magiccontrol_core,magiccontrol_platform>" magiccontrol_ui_config)
else()
    target_link_libraries(magiccontrol_battery_aging_ui_tests PRIVATE magiccontrol_core magiccontrol_platform magiccontrol_ui_config)
endif()
add_test(NAME magiccontrol_battery_aging_ui_tests COMMAND magiccontrol_battery_aging_ui_tests)
add_executable(magiccontrol_device_operation_tests test_device_operations.cpp)
# Header-only gate test: no platform library, GUI, driver or EC initialization.
target_include_directories(magiccontrol_device_operation_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/core/include")
target_compile_features(magiccontrol_device_operation_tests PRIVATE cxx_std_20)
target_link_libraries(magiccontrol_device_operation_tests PRIVATE Threads::Threads)
add_test(NAME magiccontrol_device_operation_tests COMMAND magiccontrol_device_operation_tests)
add_executable(magiccontrol_fan_preview_tests offline/test_fan_preview.cpp)
if(UNIX)
    target_link_libraries(magiccontrol_fan_preview_tests PRIVATE "$<LINK_GROUP:RESCAN,magiccontrol_core,magiccontrol_platform>")
else()
    target_link_libraries(magiccontrol_fan_preview_tests PRIVATE magiccontrol_core magiccontrol_platform)
endif()
add_test(NAME magiccontrol_fan_preview_tests COMMAND magiccontrol_fan_preview_tests)
add_test(NAME magiccontrol_architecture_routes_audit
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/architecture_audit.cmake")

add_executable(magiccontrol_sensor_decoder_tests test_sensor_decoders.cpp)
target_link_libraries(magiccontrol_sensor_decoder_tests PRIVATE magiccontrol_core)
add_test(NAME magiccontrol_sensor_decoder_tests COMMAND magiccontrol_sensor_decoder_tests)

add_executable(magiccontrol_resource_catalog_tests test_resource_catalog.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(magiccontrol_resource_catalog_tests PRIVATE "$<LINK_GROUP:RESCAN,magiccontrol_core,magiccontrol_platform>")
else()
    target_link_libraries(magiccontrol_resource_catalog_tests PRIVATE magiccontrol_core magiccontrol_platform)
endif()
add_test(NAME magiccontrol_resource_catalog_tests COMMAND magiccontrol_resource_catalog_tests)
add_test(NAME magiccontrol_resource_state_tests COMMAND magiccontrol_resource_catalog_tests --resource-state)
add_test(NAME magiccontrol_resource_state_legacy_env_ignored COMMAND magiccontrol_resource_catalog_tests --resource-state)
set_tests_properties(magiccontrol_resource_state_legacy_env_ignored PROPERTIES ENVIRONMENT "MAGICCONTROL_RESOURCE_UI=0")
add_executable(magiccontrol_settings_worker_linux_tests test_settings_worker_linux.cpp)
target_link_libraries(magiccontrol_settings_worker_linux_tests PRIVATE Threads::Threads magiccontrol_core)
add_test(NAME magiccontrol_settings_worker_linux_tests COMMAND magiccontrol_settings_worker_linux_tests)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(magiccontrol_hlyl_battery_policy_tests test_hlyl_battery_policy_linux.cpp)
    target_link_libraries(magiccontrol_hlyl_battery_policy_tests PRIVATE "$<LINK_GROUP:RESCAN,magiccontrol_core,magiccontrol_platform>")
    add_test(NAME magiccontrol_hlyl_battery_policy_tests COMMAND magiccontrol_hlyl_battery_policy_tests)
    add_executable(magiccontrol_hlyl_backend_gate_tests test_hlyl_backend_gate_linux.cpp
        ../src/platform/linux/huawei-wmi/HuaweiControlLinux.cpp)
    target_compile_definitions(magiccontrol_hlyl_backend_gate_tests PRIVATE MAGICCONTROL_HLYL_LOCK_TEST)
    target_link_libraries(magiccontrol_hlyl_backend_gate_tests PRIVATE "$<LINK_GROUP:RESCAN,magiccontrol_core,magiccontrol_platform>")
    add_test(NAME magiccontrol_hlyl_backend_gate_tests COMMAND magiccontrol_hlyl_backend_gate_tests)
    set_tests_properties(magiccontrol_hlyl_backend_gate_tests PROPERTIES TIMEOUT 15)
    add_executable(magiccontrol_hlyl_backend_sysfs_tests test_hlyl_backend_sysfs_linux.cpp
        ../src/platform/linux/huawei-wmi/HuaweiControlLinux.cpp)
    target_compile_definitions(magiccontrol_hlyl_backend_sysfs_tests PRIVATE MAGICCONTROL_HLYL_SYSFS_TEST MAGICCONTROL_TEST_ROOT="${CMAKE_BINARY_DIR}")
    target_link_libraries(magiccontrol_hlyl_backend_sysfs_tests PRIVATE "$<LINK_GROUP:RESCAN,magiccontrol_core,magiccontrol_platform>")
    add_test(NAME magiccontrol_hlyl_backend_sysfs_tests COMMAND magiccontrol_hlyl_backend_sysfs_tests)
    set_tests_properties(magiccontrol_hlyl_backend_sysfs_tests PROPERTIES TIMEOUT 20)
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TEST_GIO REQUIRED IMPORTED_TARGET gio-2.0)
    add_executable(magiccontrol_settings_sleep_dbus_tests test_settings_sleep_dbus_linux.cpp ../src/gui/linux/SettingsSleepMonitor.cpp)
    target_link_libraries(magiccontrol_settings_sleep_dbus_tests PRIVATE PkgConfig::TEST_GIO)
    add_test(NAME magiccontrol_settings_sleep_dbus_tests COMMAND magiccontrol_settings_sleep_dbus_tests)
    add_executable(magiccontrol_hwmon_resource_tests test_hwmon_resource_linux.cpp)
    target_link_libraries(magiccontrol_hwmon_resource_tests PRIVATE magiccontrol_core magiccontrol_platform)
    add_test(NAME magiccontrol_hwmon_resource_tests COMMAND magiccontrol_hwmon_resource_tests)
endif()

add_executable(magiccontrol_tests
    test_registers.cpp
    test_scroll_model.cpp
    test_core_safety.cpp
    test_temperatures.cpp
    test_board_fixture.cpp
)

if(UNIX AND NOT APPLE)
    # Core inventory and platform implementations refer to one another;
    # rescan only for this test executable's static link.
    target_link_libraries(magiccontrol_tests PRIVATE "$<LINK_GROUP:RESCAN,magiccontrol_core,magiccontrol_platform>")
else()
    target_link_libraries(magiccontrol_tests PRIVATE magiccontrol_core magiccontrol_platform)
endif()

target_include_directories(magiccontrol_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(magiccontrol_tests PROPERTIES
    OUTPUT_NAME "magiccontrol_tests"
    FOLDER "MagicControl"
)

if(MSVC)
    target_compile_options(magiccontrol_tests PRIVATE /W4)
endif()

magiccontrol_sign_target(magiccontrol_tests)

add_test(NAME magiccontrol_tests COMMAND magiccontrol_tests)

if(WIN32)
    add_executable(magiccontrol_hardware_mutex_tests test_hardware_mutex_windows.cpp)
    target_compile_features(magiccontrol_hardware_mutex_tests PRIVATE cxx_std_20)
    target_link_libraries(magiccontrol_hardware_mutex_tests PRIVATE advapi32)
    add_test(NAME magiccontrol_hardware_mutex_tests COMMAND magiccontrol_hardware_mutex_tests)
    # Manual read-only diagnostic; deliberately not registered as a test.
    add_executable(magiccontrol_gpu_power_probe gpu_power_readonly_probe.cpp)
    target_link_libraries(magiccontrol_gpu_power_probe PRIVATE magiccontrol_core magiccontrol_platform)
    add_executable(magiccontrol_wujie_power_probe wujie_power_readonly_probe.cpp)
    target_link_libraries(magiccontrol_wujie_power_probe PRIVATE magiccontrol_core magiccontrol_platform)
    add_executable(magiccontrol_settings_queue_tests test_settings_queue_windows.cpp)
    target_compile_definitions(magiccontrol_settings_queue_tests PRIVATE MAGICCONTROL_TEST_ROOT="${CMAKE_BINARY_DIR}")
    target_link_libraries(magiccontrol_settings_queue_tests PRIVATE magiccontrol_core magiccontrol_platform)
    add_test(NAME magiccontrol_settings_queue_tests COMMAND magiccontrol_settings_queue_tests)
    add_test(NAME magiccontrol_settings_preview_owner COMMAND magiccontrol_settings_queue_tests --preview-owner)
    add_executable(magiccontrol_settings_audit_tests test_settings_audit_windows.cpp)
    target_compile_definitions(magiccontrol_settings_audit_tests PRIVATE MAGICCONTROL_TEST_ROOT="${CMAKE_BINARY_DIR}")
    target_link_libraries(magiccontrol_settings_audit_tests PRIVATE magiccontrol_core magiccontrol_platform)
    add_test(NAME magiccontrol_settings_audit_tests COMMAND magiccontrol_settings_audit_tests)
    add_executable(magiccontrol_fan_performance_tests test_fan_performance_windows.cpp
        ../src/gui/windows/SettingsPage.cpp ../src/gui/windows/NumericFieldTheme.cpp ../src/gui/windows/OsdPopup.cpp)
    target_compile_definitions(magiccontrol_fan_performance_tests PRIVATE MAGICCONTROL_WINDOWS_SETTINGS_TEST)
    target_link_libraries(magiccontrol_fan_performance_tests PRIVATE magiccontrol_core magiccontrol_platform
        magiccontrol_ui_config user32 gdi32 comctl32 comdlg32 shell32 ole32 dwmapi shcore)
    add_test(NAME magiccontrol_fan_performance_tests COMMAND magiccontrol_fan_performance_tests)
    add_test(NAME magiccontrol_default_observation_tests COMMAND magiccontrol_fan_performance_tests --default-observation)
    add_test(NAME magiccontrol_fan_sleep_identity_tests COMMAND magiccontrol_fan_performance_tests --board-identity-after-sleep)
    add_test(NAME magiccontrol_fan_service_restore_tests COMMAND magiccontrol_fan_performance_tests --service-restore-failure)
    add_test(NAME magiccontrol_fan_suspend_gate_stress_tests COMMAND magiccontrol_fan_performance_tests --suspend-gate-stress)
    add_test(NAME magiccontrol_fan_sleep_presence_tests COMMAND magiccontrol_fan_performance_tests --presence-after-sleep)
    add_test(NAME magiccontrol_fan_sleep_recovery_tests COMMAND magiccontrol_fan_performance_tests --sleep-only)
    add_test(NAME magiccontrol_sets_wake_observation_tests COMMAND magiccontrol_fan_performance_tests --sets-wake-observation)
    add_test(NAME magiccontrol_sets_worker_start_failure_tests COMMAND magiccontrol_fan_performance_tests --sets-worker-start-failure)
    set_tests_properties(magiccontrol_fan_sleep_identity_tests magiccontrol_fan_service_restore_tests magiccontrol_fan_suspend_gate_stress_tests magiccontrol_fan_sleep_presence_tests magiccontrol_fan_sleep_recovery_tests
        magiccontrol_sets_wake_observation_tests magiccontrol_sets_worker_start_failure_tests PROPERTIES SKIP_RETURN_CODE 2 TIMEOUT 90)
    add_test(NAME magiccontrol_maximum_probe_tests COMMAND magiccontrol_fan_performance_tests --maximum-probe)
    add_executable(magiccontrol_power_log_tests test_power_diagnostics_windows.cpp)
    target_link_libraries(magiccontrol_power_log_tests PRIVATE magiccontrol_core magiccontrol_platform)
    add_test(NAME magiccontrol_power_log_tests COMMAND magiccontrol_power_log_tests)
    add_test(NAME magiccontrol_lenovo_power_json_offline COMMAND magiccontrol_power_log_tests --lenovo-json)
    set_tests_properties(magiccontrol_lenovo_power_json_offline PROPERTIES TIMEOUT 15 LABELS "offline;lenovo;power")
    add_executable(magiccontrol_fans_dpi_tests test_fans_dpi_windows.cpp
        ../src/gui/windows/FansPage.cpp ../src/gui/windows/NumericFieldTheme.cpp ../src/gui/windows/OsdPopup.cpp)
    target_link_libraries(magiccontrol_fans_dpi_tests PRIVATE magiccontrol_core magiccontrol_platform
        magiccontrol_ui_config user32 gdi32 comctl32 comdlg32 shell32 ole32 dwmapi shcore)
    add_test(NAME magiccontrol_fans_dpi_tests COMMAND magiccontrol_fans_dpi_tests)
    add_executable(magiccontrol_fans_resource_ui_tests test_fans_resource_windows.cpp test_board_fixture.cpp
        ../src/gui/windows/FansPage.cpp ../src/gui/windows/NumericFieldTheme.cpp ../src/gui/windows/OsdPopup.cpp)
    target_link_libraries(magiccontrol_fans_resource_ui_tests PRIVATE magiccontrol_core magiccontrol_platform
        magiccontrol_ui_config user32 gdi32 comctl32 comdlg32 shell32 ole32 dwmapi shcore)
    add_test(NAME magiccontrol_fans_resource_state COMMAND magiccontrol_fans_resource_ui_tests enabled)
    add_test(NAME magiccontrol_fans_resource_legacy_env_ignored COMMAND magiccontrol_fans_resource_ui_tests disabled)
    set_tests_properties(magiccontrol_fans_resource_state PROPERTIES ENVIRONMENT "MAGICCONTROL_RESOURCE_UI=1")
    set_tests_properties(magiccontrol_fans_resource_legacy_env_ignored PROPERTIES ENVIRONMENT "MAGICCONTROL_RESOURCE_UI=0")
    add_executable(magiccontrol_fan_buttons_tests test_fan_buttons_windows.cpp test_board_fixture.cpp
        ../src/gui/windows/FansPage.cpp ../src/gui/windows/NumericFieldTheme.cpp ../src/gui/windows/OsdPopup.cpp)
    target_link_libraries(magiccontrol_fan_buttons_tests PRIVATE magiccontrol_core magiccontrol_platform
        magiccontrol_ui_config user32 gdi32 comctl32 comdlg32 shell32 ole32 dwmapi shcore)
    add_test(NAME magiccontrol_fan_buttons_tests COMMAND magiccontrol_fan_buttons_tests)
endif()

if(WIN32)
    add_executable(magiccontrol_ec_manual_tests test_ec_manual_windows.cpp)
    target_link_libraries(magiccontrol_ec_manual_tests PRIVATE magiccontrol_core magiccontrol_platform)
    add_test(NAME magiccontrol_ec_manual_tests COMMAND magiccontrol_ec_manual_tests)
endif()

# Explicit hardware diagnostic only: never run by ctest.
if(WIN32)
    add_executable(magiccontrol_power_ab_probe ../Tools/PowerAbProbe.cpp)
    target_link_libraries(magiccontrol_power_ab_probe PRIVATE magiccontrol_core magiccontrol_platform)
    set_target_properties(magiccontrol_power_ab_probe PROPERTIES OUTPUT_NAME PowerAbProbe)
endif()

# Hardware-free regression suite; also builds standalone without platform libraries.
add_subdirectory(offline)
