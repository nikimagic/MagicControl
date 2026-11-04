#pragma once
#include "DiagnosticDump.h"
#include "GpuDevice.h"
#include <atomic>
#include <string_view>
namespace mc {
// OS inventory only. Never starts NVFlash/NVML or infers GPU identity from a
// display output. Preview models must not enable an action on another machine.
std::vector<GraphicsAdapter> vbiosDumpAdapters();
std::string vbiosAdapterPci(const GraphicsAdapter&);
bool vbiosRomDumpAvailable() noexcept;
// Optional exact PCI address (0000:01:00.0). Multiple NVIDIA devices require it.
// Default operation captures BOTH normal VBIOS and entire primary EEPROM,
// two reads each. Full EEPROM needs a known uncapped capacity <=16MiB,
// exact file sizes, equal hashes, stable identity and raw vendor compare.
// Neither result proves the firmware works or guarantees recovery.
struct VbiosRomDumpResult:DiagnosticDumpResult {
    bool vbiosSaved{},eepromSaved{},capacityVerified{};
    std::uint64_t eepromBytes{};
    // One result classification for GUI audit and CLI. A valid ordinary VBIOS
    // survives an independent EEPROM refusal; "saved" still requires both.
    std::string_view outcome() const noexcept {
        if(cancelled)return "cancelled";
        if(vbiosSaved&&eepromSaved)return "completed";
        if(vbiosSaved||eepromSaved)return "completed_partial";
        return "failed";
    }
};
VbiosRomDumpResult createVbiosRomDump(std::stop_token cancellation={},
    std::atomic<std::uint64_t>* progress=nullptr,std::string pci={},
    std::filesystem::path outputRoot={});
}
