#pragma once
#include <string_view>
#include <chrono>
#include <optional>
#include <cstdint>
namespace mc {
// Full calendar months; invalid dates and future manufacture dates have no age.
std::optional<unsigned> batteryAgeMonths(std::string_view date,std::chrono::year_month_day today) noexcept;
std::optional<unsigned> batteryAgeMonthsLocal(std::string_view date) noexcept;
// One append-only row per GUI process. Called at startup; data arrives from
// the existing monitoring owner, never from GUI hardware access.
void beginBatteryIdentityHistory(bool preferEc) noexcept;
// Collect metadata through the existing monitoring owner even when detailed
// battery logging is disabled. Stops requesting it after the startup row.
bool batteryIdentityHistoryNeedsEcSample() noexcept;
void observeBatteryIdentity(std::string_view date,std::string_view source,bool ec,
                            std::optional<std::uint64_t> thermalHistory={}) noexcept;
}
