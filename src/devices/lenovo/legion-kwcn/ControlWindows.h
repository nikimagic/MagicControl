#pragma once
#include "MagicControl/LaptopControl.h"
#include "MagicControl/Monitoring.h"
#include "../../../platform/windows/NativeFixedFans.h"
namespace mc::windows {
bool legionHostSupported();
bool legionFanWritesAllowed();
std::unique_ptr<LaptopControl> createLegionControl();
std::shared_ptr<NativeFixedFans> legionFanController();
FanDiagnosticSession prepareLegionDiagnostic();
FanMaximumResult measureLegionMaximum(std::stop_token);
FanMaximumSample readLegionFans(); // WMI getters only; safe in isolated read-only diagnostics.
EcBatteryReading readLegionEcBatteryOnly(); // Exact KWCN SRAM cache, observation only.
void requestLegionConfigurationRestore(const std::string& reason);
void suspendLegionControl();
void shutdownLegionControl();
}
