#pragma once
#include "SettingsSnapshotWindows.h"
#include "MagicControl/PortableSettings.h"
#include <array>
#include <functional>
#include <optional>
namespace mc::windows {
struct PowerSelectionContext {
    const gui::SettingsSnapshot& state;
    std::array<int,3> powerValues;
    int chosenPowerMode{},customIndex{};
    bool savedCustomIntent{},pendingPerformance{},gm6NativePower{},gm6Controller{},oemControl{},ec{},powerEditing{},wujie{};
    bool oemPolicySupported{true};
    bool nativeCustom{};
};
int selectedPowerMode(const PowerSelectionContext&);
// Button availability is presentation state only. It never grants admission
// and does not replace the model-specific writer or its readback checks.
struct PowerButtonAvailability {
    std::array<std::optional<bool>,5> modes{}; // null = leave existing control unchanged
    bool custom{},presets{},editors{},cancelEditing{};
};
struct GmPowerButtons {
    bool idle{},connected{},writable{},doubleEncoding{},codecSupported{},presetWritable{},editing{};
    std::array<bool,3> presetLimits{};
};
PowerButtonAvailability gmPowerButtonAvailability(const GmPowerButtons&,const std::function<bool()>& onBattery);
struct HonorPowerButtons {bool idle{},stale{},presetWritable{},editing{};std::uint32_t tableVersion{};};
PowerButtonAvailability honorPowerButtonAvailability(const HonorPowerButtons&,const std::function<bool()>& onBattery);
struct NativePowerButtons {bool idle{},connected{},stockWritable{},customWritable{},editing{};int stockCount{};};
PowerButtonAvailability nativePowerButtonAvailability(const NativePowerButtons&) noexcept;
struct EcPowerButtons {bool enabled{},fanDefaultLocked{},needsRestore{},wujie{},hasController{},editing{};};
PowerButtonAvailability ecPowerButtonAvailability(const EcPowerButtons&) noexcept;
struct PowerStartupState {
    std::array<int,3> values{25,25,30};
    int chosenMode{-1};
    bool editorsInitialized{},savedCustomIntent{};
};
// Pure mapping; recipe resolution is supplied by the caller so its power
// source and model policy remain exactly those of the admitted backend.
PowerStartupState decodePowerStartup(PowerStartupState,const DevicePreferences&,bool loaded,
    bool wujie,const std::optional<CustomPreset>& honorPreset,
    const std::function<std::array<int,3>(int)>& honorRecipe);
// Read preferences and, when requested, the Honor preset in the former page
// order. This function never applies a profile or touches fan/EC state.
PowerStartupState loadPowerStartup(std::string_view profile,bool allowPreferences,bool honor,
                                  bool wujie,PowerStartupState initial);
}
