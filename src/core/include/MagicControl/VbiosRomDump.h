#pragma once
#include "DiagnosticDump.h"
#include "GpuDevice.h"
#include "TelemetryGate.h"
#include <atomic>
#include <string_view>
namespace mc {
// OS inventory only. Never starts NVFlash/NVML or infers GPU identity from a
// display output. Preview models must not enable an action on another machine.
std::vector<GraphicsAdapter> vbiosDumpAdapters();
std::string vbiosAdapterPci(const GraphicsAdapter&);
bool vbiosRomDumpAvailable() noexcept;
// Optional exact PCI address (0000:01:00.0). Multiple NVIDIA devices require it.
// Each operation reads only the requested image, twice. Full EEPROM needs a known uncapped capacity <=16MiB,
// exact file sizes, equal hashes, stable identity and raw vendor compare.
// Neither result proves the firmware works or guarantees recovery.
enum class VbiosDumpMode { Image, EntireEeprom };
struct VbiosRomDumpResult:DiagnosticDumpResult {
    VbiosDumpMode mode{VbiosDumpMode::Image};
    bool vbiosSaved{},eepromSaved{},capacityVerified{};
    std::uint64_t eepromBytes{};
    bool requestedImageSaved() const noexcept {
        return mode==VbiosDumpMode::EntireEeprom?eepromSaved:vbiosSaved;
    }
    std::string_view outcome() const noexcept {
        if(cancelled)return "cancelled";
        return requestedImageSaved()?"completed":"failed";
    }
};
VbiosRomDumpResult createVbiosRomDump(std::stop_token cancellation={},
    std::atomic<std::uint64_t>* progress=nullptr,std::string pci={},
    std::filesystem::path outputRoot={},std::shared_ptr<TelemetryGate::FirmwarePause> pause={},
    VbiosDumpMode mode=VbiosDumpMode::Image);
}
