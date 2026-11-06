# ---------------------------------------------------------------------------
# MagicControl core - platform independent logic.
#
# This library never calls an operating system API directly. Everything that
# touches hardware goes through typed IEcBackend/ILiquidCoolingBackend adapters
# implemented by src/platform/*.
# The Windows GUI and the future Kubuntu Wayland GUI both link this target.
# ---------------------------------------------------------------------------
# Atomic file replacement is a leaf OS adapter used by core settings/inventory.
# Keep it outside the broad platform archive: platform itself depends on core,
# and a core -> platform symbol produced an order-dependent Linux CLI link.
if(WIN32)
    set(_storage_adapter ../platform/windows/PlatformStorageWindows.cpp)
else()
    set(_storage_adapter ../platform/linux/PlatformStorageLinux.cpp)
endif()
add_library(magiccontrol_storage STATIC ${_storage_adapter})
target_include_directories(magiccontrol_storage PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(magiccontrol_storage PUBLIC cxx_std_20)
set_target_properties(magiccontrol_storage PROPERTIES FOLDER "MagicControl")

add_library(magiccontrol_core STATIC
    src/Types.cpp
    src/ResourceCatalog.cpp
    src/ResourceCatalogJson.cpp
    src/ResourceProvider.cpp
    src/Errors.cpp
    src/IEcBackend.cpp
    src/EcAccess.cpp
    src/BoardIdentity.cpp
    src/HardwareInventory.cpp
    src/EcIdentityInventory.cpp
    src/FanCurve.cpp
    src/ApplicationPresence.cpp
    src/ScrollModel.cpp
    src/PortableSettings.cpp
    src/KeysLifecycle.cpp
    src/CustomPresets.cpp
    src/Logging.cpp
    src/ActionLog.cpp
    src/BatteryIdentityHistory.cpp
    src/BatteryAging.cpp
    src/ControlAudit.cpp
    src/RomIdHistory.cpp
    src/KeyboardBacklight.cpp
    src/LiquidCooling.cpp
    ../protocols/uniwill/RamFanFactory.cpp
    ../protocols/uniwill/QuickSwitch.cpp
    ../protocols/uniwill/x5sp4nag/NativeDefaultFan.cpp
    ../protocols/uniwill/gm6ir0c/NativeDefaultFan.cpp
)

target_include_directories(magiccontrol_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(magiccontrol_core PUBLIC Threads::Threads PRIVATE magiccontrol_storage)

target_compile_features(magiccontrol_core PUBLIC cxx_std_20)

set_target_properties(magiccontrol_core PROPERTIES
    OUTPUT_NAME magiccontrol_core
    FOLDER "MagicControl"
)

if(MSVC)
    target_compile_options(magiccontrol_core PRIVATE /W4)
endif()

message(STATUS "  core          : magiccontrol_core (static)")
