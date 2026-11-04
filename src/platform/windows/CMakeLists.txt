# ---------------------------------------------------------------------------
# Windows platform backend.
#
# Talks straight to the ACPI driver that the OEM Control Center installs:
#   device    \\.\ACPIDriver
#   IOCTL     read 0x9C40A488, write 0x9C40A48C
#   packet    8 bytes in  (address DWORD, value DWORD)
#   reply     16 bytes out, the read byte sits at offset 0
#
# The C# reference implementation called the OEM's ACPIDriverDll.dll. This
# backend issues the same IOCTLs directly, so MagicControl does not need that
# DLL, and the input packet stays at 8 bytes instead of the DLL's 4 MiB buffer.
# ---------------------------------------------------------------------------
include("${PROJECT_SOURCE_DIR}/cmake/EmbeddedRuntime.cmake")
add_library(magiccontrol_runtime STATIC EmbeddedRuntimeWindows.cpp)
target_include_directories(magiccontrol_runtime PUBLIC "${PROJECT_SOURCE_DIR}/src/core/include" "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(magiccontrol_runtime PRIVATE bcrypt advapi32 shell32 ole32 uuid)
target_compile_features(magiccontrol_runtime PUBLIC cxx_std_20)

add_library(magiccontrol_platform STATIC
    ../common/Overclock.cpp
    ../common/NvidiaGpu.cpp
    ../common/DiscreteGpuOperation.cpp
    CustomModeWindows.cpp
    SettingsPersistenceWindows.cpp
    SettingsPowerSelectionWindows.cpp
    KeysHardwareWindows.cpp
    SettingsOperationsWindows.cpp
    SettingsActionWindows.cpp
    EcLoggerWindows.cpp
    SettingsSnapshotWindows.cpp
    LightingControlWindows.cpp
    FanDeviceActionsWindows.cpp
    FanMonitoringWindows.cpp
    FanCurveStorageWindows.cpp
    InfoOperationsWindows.cpp
    ApplicationPreferencesWindows.cpp
    BatterySettingsWindows.cpp
    ../common/GpuDevice.cpp
    GraphicsAdaptersWindows.cpp
    GpuDisplayRoutesWindows.cpp
    RadeonGpuWindows.cpp
    IntelGpuWindows.cpp
    NvidiaSensorsWindows.cpp
    AdlxRuntime.cpp
    ../common/DiagnosticDump.cpp
    ../common/InfoDumpResource.cpp
    ../common/PowerInputDump.cpp
    ../common/FanMaximum.cpp
    DiagnosticDumpWindows.cpp
    PawnIoStatusWindows.cpp
    ResourceDriversWindows.cpp
    NativeBatteryWindows.cpp
    IteLightingWindows.cpp
    LiquidCoolingWindows.cpp
    BatteryEnforcementWindows.cpp
    RomIdWindows.cpp
    RamFanFactoryWindows.cpp
    DiagnosticWorkerWindows.cpp
    FirmwareEvidenceWindows.cpp
    BiosRomDumpWindows.cpp
    AmiBiosDumpWindows.cpp
    NvflashWindows.cpp
    ../common/VbiosRomDump.cpp
    ../common/BiosRomDump.cpp
    DiagnosticSensorsWindows.cpp
    DiagnosticWmiWindows.cpp
    uniwill-acpi/BackendWindows.cpp
    BackendFactoryWindows.cpp
    huawei-wmi/HuaweiWmi.cpp
    ../../devices/huawei/hlyl-wxx9/ControlWindows.cpp
    ../../devices/huawei/hlyl-wxx9/BacklightWindows.cpp
    ../../devices/huawei/hlyl-wxx9/I2EcWindows.cpp
    ../../devices/huawei/hlyl-wxx9/FanControlWindows.cpp
    ../../devices/mechrevo/gm6ir0c/ControlWindows.cpp
    ../../devices/eluktronics/hydroc-16-g2/DiagnosticsWindows.cpp
    pawnio/LpcIoWindows.cpp
    ../../devices/minisforum/hpbsd/TelemetryWindows.cpp
    ../../devices/minisforum/hpbsd/I2EcWindows.cpp
    ../../devices/minisforum/hpbsd/FanControlWindows.cpp
    ../../devices/minisforum/hpbsd/PowerControlWindows.cpp
    ../../devices/lenovo/legion-kwcn/ControlWindows.cpp
    ../../devices/lenovo/legion-kwcn/WmiWindows.cpp
    ../../devices/lenovo/legion-kwcn/I2EcWindows.cpp
    NativeFixedFans.cpp
    BoardIdentityWindows.cpp
    InventoryWindows.cpp
    InventoryDetailsWindows.cpp
    WindowsDevices.cpp
    MicrophoneControlWindows.cpp
    MicrophoneKeysWindows.cpp
    OemEventMonitor.cpp
    OsdSystemMonitor.cpp
    WindowsDisplay.cpp
    CopilotFirmwareWindows.cpp
    MonitoringWindows.cpp
    PowerDiagnosticsWindows.cpp
    AmdTelemetryWindows.cpp
    pawnio/PawnTelemetryWindows.cpp
    pawnio/IntelTelemetryWindows.cpp
    pawnio/IntelMemoryWindows.cpp
    pawnio/IntelPchWindows.cpp
    pawnio/ApuPowerWindows.cpp
    ../../devices/mechrevo/x5sp4nag/windows/BatteryChargeWindows.cpp
    ../../devices/mechrevo/x5sp4nag/windows/FanControlWindows.cpp
    ConfigurationRestoreWindows.cpp
)
target_include_directories(magiccontrol_platform PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# Reuse the packaging dependency list in diagnostics, including CLI-only builds.
include("${PROJECT_SOURCE_DIR}/cmake/PawnIORuntime.cmake")
set(_pawnio_header "#pragma once\n#include <array>\n#include <string_view>\nnamespace mc::dependencies {\ninline constexpr auto requiredRuntimeFiles = std::to_array<std::string_view>({\n")
foreach(_file IN LISTS MAGICCONTROL_PAWNIO_REQUIRED_FILES)
    string(APPEND _pawnio_header "    \"${_file}\",\n")
endforeach()
string(APPEND _pawnio_header "});\n}\n")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/MagicControl/PawnIoRuntimeFiles.h" CONTENT "${_pawnio_header}" @ONLY)

target_link_libraries(magiccontrol_platform
    PUBLIC magiccontrol_core
    PRIVATE magiccontrol_runtime
    PRIVATE advapi32 ole32 oleaut32 wbemuuid setupapi cfgmgr32 tbs windowsapp user32 powrprof uuid bcrypt dxgi version wintrust crypt32
)

target_compile_features(magiccontrol_platform PUBLIC cxx_std_20)

set_target_properties(magiccontrol_platform PROPERTIES
    OUTPUT_NAME magiccontrol_platform
    FOLDER "MagicControl"
)

if(MSVC)
    target_compile_options(magiccontrol_platform PRIVATE /W4)
endif()

message(STATUS "  platform      : magiccontrol_platform (windows-acpi-driver)")
