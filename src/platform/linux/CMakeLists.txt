# ---------------------------------------------------------------------------
# Linux platform backend.
#
# PLANNED - to be written on Kubuntu 26.04 against real hardware.
#
# Transport: ec_sys loaded with write_support=1 exposes
#   /sys/kernel/debug/ec/ec0/io
# which maps the 256-byte EC address space via ec_read()/ec_write().
#
# Every register this project touches lives at 0x43E..0xF5F, outside that
# 256-byte window. The transport must expose the full board-specific address
# space; ec_sys alone cannot implement it. Unreachable addresses must raise
# AddressNotReachable instead of silently failing.
#
# Open question for the port: the battery charge limit is driven through the
# ACPI WMI address space on Windows (0x7A6, 0x87F), which ec_sys does not
# expose. On Linux that path must move to the uniwill-laptop driver sysfs
# attributes (charge_control_end_threshold / charge_types).
# ---------------------------------------------------------------------------
message(STATUS "  platform      : not implemented yet - see src/platform/linux/README.md")

# The Linux hardware inventory is a stub that reports itself as unimplemented, so
# the BIOS page compiles and renders an empty list on Linux instead of failing.
add_library(magiccontrol_platform STATIC
    ../common/Overclock.cpp
    FansServicesLinux.cpp
    ../common/DiscreteGpuOperation.cpp
    SettingsServicesLinux.cpp
    KeysControlLinux.cpp
    ../common/NvidiaGpu.cpp
    ../common/GpuDevice.cpp
    GraphicsAdaptersLinux.cpp
    RadeonGpuLinux.cpp
    IntelGpuLinux.cpp
    HwmonLinux.cpp
    HwmonResourceProvider.cpp
    ../common/DiagnosticDump.cpp
    ../common/InfoDumpResource.cpp
    ../common/PowerInputDump.cpp
    ../common/FanMaximum.cpp
    DiagnosticDumpLinux.cpp
    DiagnosticWorkerLinux.cpp
    FirmwareEvidenceLinux.cpp
    BiosRomDumpLinux.cpp
    NvflashLinux.cpp
    ../common/VbiosRomDump.cpp
    ../common/BiosRomDump.cpp
    DiagnosticSensorsLinux.cpp
    BackendLinux.cpp
    RomIdLinux.cpp
    BoardIdentityLinux.cpp
    InventoryLinux.cpp
    MonitoringLinux.cpp
    LiquidCoolingLinux.cpp
    huawei-wmi/HuaweiControlLinux.cpp
)

target_link_libraries(magiccontrol_platform PUBLIC magiccontrol_core PRIVATE ${CMAKE_DL_LIBS})
find_package(PkgConfig REQUIRED)
pkg_check_modules(FIRMWARE_GLIB REQUIRED IMPORTED_TARGET glib-2.0)
target_link_libraries(magiccontrol_platform PRIVATE PkgConfig::FIRMWARE_GLIB)

if(MAGICCONTROL_BUILD_GUI)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SYSTEM_EVENTS REQUIRED IMPORTED_TARGET libpulse gio-2.0)
    target_sources(magiccontrol_platform PRIVATE OsdSystemMonitor.cpp MicrophoneControlLinux.cpp DisplayControlLinux.cpp DisplayHotkeyLinux.cpp)
    target_link_libraries(magiccontrol_platform PRIVATE PkgConfig::SYSTEM_EVENTS)
endif()
target_compile_features(magiccontrol_platform PUBLIC cxx_std_20)

set_target_properties(magiccontrol_platform PROPERTIES
    OUTPUT_NAME magiccontrol_platform
    FOLDER "MagicControl"
)
