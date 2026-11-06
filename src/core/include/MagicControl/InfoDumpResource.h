#pragma once
#include "BiosRomDump.h"
#include "PowerInputDump.h"
#include "VbiosRomDump.h"

namespace mc {
enum class InfoDumpKind { Ec, Bios, Vbios, PowerInput };
// An explicit request for the actual machine. No resource key or model label
// admits hardware access; each existing collector keeps its own safeguards.
struct InfoDumpResourceRequest {
    InfoDumpKind kind{InfoDumpKind::Ec};
    std::stop_token cancellation;
    std::atomic<std::uint64_t>* progress{};
    std::string pci;
    std::filesystem::path outputRoot;
    std::shared_ptr<EcAccess> access;
    MaximumFanProbe maximumProbe;
    std::shared_ptr<std::mutex> hardwareGate;
    std::shared_ptr<TelemetryGate::FirmwarePause> firmwarePause;
    DiagnosticDumpMode mode{DiagnosticDumpMode::ReadOnly};
    FirmwareFanDefault firmwareDefault;
    std::string displaySnapshot;
    PowerInputDumpSession* powerSession{};
    PowerInputPrompt prompt;
};
struct InfoDumpResourceResult {DiagnosticDumpResult report;VbiosRomDumpResult vbios;};
// Injection is limited to test fixtures or already-owned collectors. It does
// not select a driver, infer compatibility, or alter the caller's reservation.
struct InfoDumpResourceServices {
    std::function<DiagnosticDumpResult(const InfoDumpResourceRequest&)> ec,bios,powerInput;
    std::function<VbiosRomDumpResult(const InfoDumpResourceRequest&)> vbios;
};
InfoDumpResourceResult executeInfoDumpResource(const InfoDumpResourceRequest&,const InfoDumpResourceServices* services=nullptr);
}
