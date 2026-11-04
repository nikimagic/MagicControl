#pragma once
#include "DiagnosticDump.h"
#include <atomic>
#include <string>
namespace mc {
// Action availability, independent of model selection. Capture checks actual BIOS.
bool biosRomDumpAvailable();
std::uint64_t biosRomExpectedBytes(); // SMBIOS declaration only; no device IO.
// Explicit AMI save action, two passes and durable diagnostic artifacts.
// saved means two verified equal captures were saved. Intel /O can return only
// a UEFI region: manifest capture_kind/region_only/capture_complete and message
// distinguish that result from a full ROM image. Never infer SPI size from saved.
DiagnosticDumpResult createBiosRomDump(std::stop_token cancellation={},
    std::atomic<std::uint64_t>* progress=nullptr);
DiagnosticDumpResult createPlatformBiosRomDump(std::stop_token,std::atomic<std::uint64_t>*);
// Legacy fixed MMRD wire capture only. Not the GUI AFU capture size.
inline constexpr std::uint32_t biosRomWindowBytes=16*1024*1024;
}
