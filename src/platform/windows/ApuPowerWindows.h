#pragma once

#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace mc::windows {
struct ApuPowerDiagnostic {
    std::optional<double> fastPowerWatts, stapmLimitWatts, fastLimitWatts, slowLimitWatts;
    std::uint32_t tableVersion{};
    bool stale{};
    std::string status;
    friend bool operator==(const ApuPowerDiagnostic&, const ApuPowerDiagnostic&) = default;
};

// read() is telemetry only. HLYL writes are a separate explicit user action,
// guarded by exact board/CPUID/PM layout and verified through the PM table.
// Called serially by the SETs worker; shares the standard PCI mutex with PawnIO.
class ApuPowerReader {
public:
    ApuPowerReader();
    ~ApuPowerReader();
    ApuPowerDiagnostic read(bool verifiedBoard);
    ApuPowerDiagnostic applyHlylLimits(const std::array<int,3>& stapmSlowFast);
    // Exact milliwatts allow the diagnostic tool to restore factory fractional
    // limits (for example37.8 W). Same identity, range and PM guards as watts.
    ApuPowerDiagnostic applyHlylLimitsMilliwatts(const std::array<int,3>& stapmSlowFast);
    // HPBSD's shared Custom/diagnostic transport. Common request bounds do not
    // prove a physical ceiling; exact identity/PM, ACK/readback/rollback remain.
    ApuPowerDiagnostic applyHpbsdLimitsMilliwatts(const std::array<int,3>& stapmSlowFast);
    // Compiled BIOS stock endpoints use the same exact transport, baseline,
    // ACK/readback and rollback machinery; no invented cross-model recipes.
    ApuPowerDiagnostic applyHpbsdStockProfile(int profile);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
