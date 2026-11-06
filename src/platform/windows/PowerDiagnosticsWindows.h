#pragma once
#include <windows.h>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace mc { class EcAccess; struct MonitoringSnapshot; struct BatteryPowerPolicy; }
namespace mc::windows {
// Legacy name for the shared Intel/optional Lenovo JSON serializer; no hardware access.
[[nodiscard]] std::string lenovoPowerDiagnostic(const MonitoringSnapshot& sample);
// Shared typed policy reader for the battery card and diagnostic JSON. Read-only.
BatteryPowerPolicy readBatteryPowerPolicy(const GUID* activeScheme=nullptr);
// Windows notifications only enqueue journal entries; they never access EC.
class PowerDiagnostics {
public:
    explicit PowerDiagnostics(HWND window);
    ~PowerDiagnostics();
    PowerDiagnostics(const PowerDiagnostics&) = delete;
    PowerDiagnostics& operator=(const PowerDiagnostics&) = delete;
    void broadcast(WPARAM event, LPARAM data) noexcept;
    [[nodiscard]] std::filesystem::path path() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Reuses the existing monitoring worker and EcAccess session. Read-only.
void recordPowerSample(EcAccess* access, const MonitoringSnapshot& sample) noexcept;
void recordPowerAction(std::string_view action, std::string_view phase, int requested,
                       std::string_view detail = {}) noexcept;
[[nodiscard]] bool powerDiagnosticBurstActive() noexcept;
// Read-only event generation; consumers can cap their own detail output even
// if the main monitoring consumer is paused. Does not start/reset a burst.
[[nodiscard]] ULONGLONG powerDiagnosticBurstGeneration() noexcept;
} // namespace mc::windows
