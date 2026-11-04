#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>

namespace mc {
inline std::atomic<bool>& logSelection() noexcept {
    static std::atomic<bool> enabled{true};
    return enabled;
}
inline bool loggingEnabled() noexcept { return logSelection().load(std::memory_order_relaxed); }
inline void setLoggingEnabled(bool enabled) noexcept { logSelection().store(enabled, std::memory_order_relaxed); }
// BatteryLog is independent of AllLogs. Disabling it also hides SETs diagnostics.
inline std::atomic<bool>& batteryLogSelection() noexcept { static std::atomic<bool> enabled{false};return enabled; }
inline bool batteryLoggingEnabled() noexcept { return batteryLogSelection().load(std::memory_order_relaxed); }
inline void setBatteryLoggingEnabled(bool enabled) noexcept { batteryLogSelection().store(enabled,std::memory_order_relaxed); }
inline bool anyLoggingEnabled() noexcept { return loggingEnabled()||batteryLoggingEnabled(); }
// One pair per launch: MagicControl-N.jsonl/battery-N.jsonl, oldest of3 slots.
// Active slots are process-leased; a fourth concurrent logger fails safely.
// No in-session .part rotation. Legacy logs are archived without overwriting.
bool initializeLogging(bool enabled, const std::filesystem::path& directory = {},bool batteryEnabled=false) noexcept;
// Initialization failure reason, including all three pairs being active.
[[nodiscard]] std::string loggingInitializationError();
// Actual SMBIOS board, including unknown/NoName devices; never UI preview.
// logs_<board>, then product name, then Unknown. Does not move previous logs.
[[nodiscard]] std::filesystem::path defaultLogDirectory();
[[nodiscard]] std::filesystem::path currentLogPath();
[[nodiscard]] std::filesystem::path currentBatteryLogPath();
// Flush/copy current pair to session.jsonl and battery.jsonl in a dump folder
// under one gate, without rotating/truncating either live journal.
bool snapshotCurrentLog(const std::filesystem::path& destination,std::string& error) noexcept;
void recordLog(std::string_view channel, std::string_view jsonFields) noexcept;
void logMessage(std::string_view channel, std::string_view message) noexcept;
} // namespace mc
