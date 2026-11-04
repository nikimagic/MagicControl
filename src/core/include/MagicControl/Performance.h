#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace mc {
class EcAccess;
struct EcPowerLimits {
    // Historical EC/OEM order PL1/PL2/PL4. For overrides on verified X5SP4NAG
    // this maps to STAPM/Slow/Fast, NOT STAPM/Fast/Slow. Factory preset values
    // are not a guarantee of current SMU limits; see src/core/src/POWER_MODES.md.
    std::optional<int> sustained, shortBoost, peak;
    friend bool operator==(const EcPowerLimits&, const EcPowerLimits&) = default;
};
struct EcPerformanceState {
    std::uint8_t rawMode{};
    std::array<EcPowerLimits, 3> presets;
    EcPowerLimits active;
    EcPowerLimits overrides;
    // Exact Wujie82/72/26 only: post-policy command cache46A/46B/46F,
    // STAPM/Slow/Fast. Separate from requested overrides and SMU readback.
    // Missing/failed observations must never fall back to active/preset values.
    EcPowerLimits accepted;
    bool acceptedValid{};
    std::string acceptedStatus;
    // WUJIE overrides retain successfully read zero bytes (firmware/Auto).
    // active keeps the native preset field wherever its override is zero.
    friend bool operator==(const EcPerformanceState&, const EcPerformanceState&) = default;
};
// Legacy fixed presets retained for old saved settings and unchanged Minimal.
// New Office/Gaming/Turbo selections use the typed model-native CPU path.
// Storage/register order STAPM / Slow / Fast; these are not universal OEM limits.
inline constexpr std::array<std::array<int, 3>, 4> kApuPowerPresets{{
    {10, 10, 15}, {25, 25, 30}, {40, 40, 45}, {65, 65, 70}}};
// Battery ceiling, internal STAPM / Slow / Fast order. AC retains its presets.
inline constexpr std::array<int, 3> kApuBatteryMaximum{40, 40, 45};
// OEM EC setters cast PL1/PL2/PL4 to byte. This bounds encoding, not proven SMU
// acceptance. Preset DC ceilings are separate from Custom; never wrap above255.
inline constexpr int kApuManualMaximum=255;
inline std::array<int, 3> apuPresetLimits(int preset, bool) {
    return kApuPowerPresets.at(preset);
}
struct PerformanceChange { std::uint8_t before{}, after{}; };
EcPerformanceState readEcPerformance(EcAccess& access);
// The OEM selection clears custom power overrides, preserving fan ownership
// and all RAM fan points. Coordinate access with the existing operation owner,
// but never reapply fan modes or tables because an APU profile changed.
PerformanceChange selectEcPerformance(EcAccess& access, int preset);
// User-entered watts in STAPM/Slow/Fast order. Writes only 783/784/785.
// Current exact-board implementation requires direct PL4 scale (727.7 clear).
void applyManualPowerLimits(EcAccess& access, const std::array<int, 3>& watts);
// Select the saved Custom CPU base before overrides, preserving FAN fields.
PerformanceChange applyCustomPowerLimits(EcAccess& access,const std::array<int,3>& watts);
PerformanceChange selectWujieCustomBase(EcAccess& access,int base);
// WUJIE Auto clears only783/784/785, preserving751 and all FAN fields.
void resetManualPowerLimits(EcAccess& access);
} // namespace mc
