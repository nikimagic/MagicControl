#pragma once
#include <filesystem>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include "FanMaximum.h"
namespace mc {
class EcAccess;
enum class DiagnosticDumpMode {WithFanProbes,ReadOnly};
// Actual collector profile only, never a GUI preview. An explicit --probes
// cannot override a device diagnostic-probe restriction. Other devices keep their policy.
inline constexpr DiagnosticDumpMode effectiveDiagnosticDumpMode(std::string_view profile,DiagnosticDumpMode requested)noexcept{
    return profile=="minisforum-hpbsd"?DiagnosticDumpMode::ReadOnly:requested;
}
// Lifecycle admission only, independent of lighting state or EC register maps.
inline std::atomic<std::uint64_t> diagnosticFanEpochCounter{};
inline std::uint64_t diagnosticFanEpoch() noexcept { return diagnosticFanEpochCounter.load(); }
inline void invalidateDiagnosticFanSessions() noexcept { ++diagnosticFanEpochCounter; }
struct FanDiagnosticRestore {
    bool attempted{},confirmed{};
    std::string status;
    bool superseded{}; // A newer explicit command/epoch replaces the old intent.
};
struct FanDiagnosticSession {
    std::string previousMode;
    std::function<FanDiagnosticRestore()> restore;
};
// The active typed owner captures its confirmed intent before Default. The
// returned one-shot callback revalidates ownership/generation before replay.
using FirmwareFanDefault=std::function<FanDiagnosticSession()>;
struct DiagnosticDumpResult {
    std::filesystem::path directory;
    bool saved{},ecComplete{},cancelled{},hardwareWarning{},paused{};
    std::string message;
    // File collection and FAN restoration are independent outcomes.
    std::string hardwareWarningMessage;
};
// Explicit user action, actual hardware identity only. A diagnostic snapshot is
// not a BIOS/VBIOS flash image. It includes the user-requested maximum-fan
// probes. First return FANs to firmware Default, capture EC, then collect
// telemetry and hold Max30s. Probe cleanup returns to Default. After all data,
// the same typed owner may restore its confirmed pre-dump FANs intent, provided
// no newer command/power epoch/foreign owner superseded it. Cancellation before
// restore admission or unconfirmed cleanup never replays old Manual. An admitted
// logical write completes its readback/cleanup instead of leaving half a table.
// No voltage/charge writes or broad settings replay.
// After the initial EC and telemetry baseline, exact supported models can
// export factory RAM1.5 tables through their guarded OEM mailbox before Max.
// ReadOnly omits Default, factory mailbox and EC/NVML fan probes.
// HPBSD is forced to ReadOnly inside the collector, including --probes:
// no Default, factory mailbox, Max, FAN restore or settings replay from the dump.
// Lenovo uses the same Default -> snapshot -> native Max -> Default -> restore
// sequence as Wujie; its Maximum resource command changes FNST, never a curve.
// Both modes collect at least60 seconds after the first saved telemetry sample,
// unless cancelled or the recorder fails. Probe time counts toward that window.
// Shared access keeps the existing EC instance alive if its UI is closed.
// Optional display snapshot is captured by a GUI main-thread OS adapter before
// starting this worker. Empty selects the platform getter/fallback (including CLI).
DiagnosticDumpResult createDiagnosticDump(std::shared_ptr<EcAccess> access={},std::stop_token cancellation={},MaximumFanProbe maximumFanProbe={},std::shared_ptr<std::mutex> hardwareGate={},DiagnosticDumpMode mode=DiagnosticDumpMode::WithFanProbes,FirmwareFanDefault firmwareDefault={},std::string displaySnapshot={},std::filesystem::path outputRoot={});
}
