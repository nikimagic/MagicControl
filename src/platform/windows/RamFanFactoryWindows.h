#pragma once
#include "../../protocols/uniwill/RamFanFactory.h"
#include <stop_token>
#include <string>
namespace mc::windows {
struct HydrocDiagnosticDefaultResult {bool attempted{},confirmed{};std::string status;};
// Explicit diagnostic admission only, never a general HYDROC FAN capability.
// Caller owns DeviceOperation. Default touches four masks, never RAM/APP/mode.
HydrocDiagnosticDefaultResult prepareHydrocDiagnosticDefault(bool explicitWithFanProbes,std::stop_token={});
// Call only after initial EC snapshot, with explicit WithFanProbes permission.
// Function holds the cross-process EC transaction for the complete mailbox flow.
uniwill::RamFanFactoryResult exportHydrocFactoryTables(bool explicitWithFanProbes,std::stop_token={});
}
