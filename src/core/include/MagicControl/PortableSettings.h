#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <stdexcept>
#include "MagicControl/EcRegister.h"
#include "MagicControl/CustomPresets.h"

namespace mc {
struct ApplicationPreferences {
    // osdDisabled is an application-owned runtime view of keys-settings.ini.
    // Copilot is hardware KEYs state; it must not have a second application DTO.
    // saveApplicationPreferences persists only the non-KEYs application fields.
    bool osdDisabled{}, embeddedFont{true}, allLogs{true}, batteryLog{true};
    unsigned language{}; // 0 RU, 1 EN, 2 simplified Chinese, 3 traditional Chinese.
    friend bool operator==(const ApplicationPreferences&, const ApplicationPreferences&) = default;
};
enum class DeviceSetting : std::size_t {
    // ChargeAuto:0=4/FF,1=7/04,2=7/FF; only admitted board policy writers use it.
    ChargeLimit, Performance, KeyboardLevel, KeyboardEffect, PulseColor,
    Red, Green, Blue, WindowsLocked, FnLocked, UsbCharging, AcRecovery,
    Wifi, Bluetooth, Camera, Touchpad,
    ManualPowerEnabled, ManualStapm, ManualSlow, ManualFast, ApuMode, ChargeAuto, BatteryProtection, ChargeRestart, KeyboardCycleEnabled, BatteryPercentageEnabled, BatteryCardView, NativeCpuPresets, ApuAutoPower, CustomBase, Count
};
struct DevicePreferences {
    std::array<std::optional<int>, static_cast<std::size_t>(DeviceSetting::Count)> values{};
    std::optional<int>& operator[](DeviceSetting key) { return values[static_cast<std::size_t>(key)]; }
    const std::optional<int>& operator[](DeviceSetting key) const { return values[static_cast<std::size_t>(key)]; }
};
bool loadDevicePreferences(const std::filesystem::path&, DevicePreferences&, std::string& error);
std::filesystem::path batteryPreferencesPath(std::string_view profileId);
int selectedChargeRestart(std::string_view profileId,int fallback);
void rememberChargeRestart(std::string_view profileId,int percent);
[[nodiscard]] bool customBaseSupported(std::string_view profileId) noexcept;
[[nodiscard]] int selectedCustomBase(std::string_view profileId);
void rememberCustomBase(std::string_view profileId,int base);
// Atomically merge confirmed user choices. Passive EC reads never update this file.
bool rememberDevicePreferences(const DevicePreferences& patch, std::string& error,const std::filesystem::path& target={});
static_assert(std::tuple_size_v<FanCurveTable> == ec::kTablePointCount);

enum class FanCurvePreset : std::size_t { Manual, Default, Max, Min };
inline constexpr std::array<std::string_view, 4> kFanCurvePresetNames{"Manual", "Default", "Max", "Min"};
inline constexpr std::array<FanCurvePreset, 4> kFanCurvePresetOrder{
    FanCurvePreset::Default, FanCurvePreset::Manual, FanCurvePreset::Min, FanCurvePreset::Max};
struct FanCurveProfiles {
private:
    std::array<int,2> activePoints_{16,16};
public:
    std::array<std::array<FanCurveTable, 2>, 4> curves{};
    std::array<FanCurvePreset, 2> selected{FanCurvePreset::Min, FanCurvePreset::Min};
    // Links the editor curves; the actual EC split bit is read independently.
    bool linked{};
    FanCurveTable ecManualCurve{}; // One independent common curve for autonomous EC control.
    bool ecManual{}; // Autonomous EC common curve, distinct from host Manual.
    bool restoreSelected{}; // Explicit confirmed FAN intent, never inferred from telemetry.
    FanCurveProfiles();
    // Owner contract:16/16 by default for ALL devices. Counts are device data;
    // changing them NEVER resamples, normalizes or erases stored curve cells.
    void setActivePointCount(std::size_t channel,int count){
        if(channel>=2||count<1||count>16)throw std::out_of_range("FAN active point count1..16");activePoints_[channel]=count;
    }
    [[nodiscard]] int activePointCount(std::size_t channel)const{return activePoints_.at(channel);}
    [[nodiscard]] FanCurvePreset currentPreset(std::size_t channel) const {
        return selected[linked ? 0 : channel];
    }
    [[nodiscard]] const FanCurveTable& current(std::size_t channel) const {
        if (ecManual) return ecManualCurve;
        if (linked) channel = 0;
        return curves[static_cast<std::size_t>(selected[channel])][channel];
    }
    void select(std::size_t channel, FanCurvePreset preset) { ecManual = false; selected[linked ? 0 : channel] = preset; }
    void edit(std::size_t channel, const FanCurveTable& table);
    friend bool operator==(const FanCurveProfiles&, const FanCurveProfiles&) = default;
};

/// Known settings live in settings/ beside the executable on both OSes.
/// Assets, logs and diagnostic files remain executable-relative.
[[nodiscard]] std::filesystem::path portableSettingsPath(std::string_view filename);
// Read-only compatibility lookup for files from releases before settings/.
std::filesystem::path settingsReadPath(const std::filesystem::path&);
// GUI startup migration, after exclusive instance admission on Windows.
// Preserve sources; first handoff imports previous settings over clean templates.
void migratePortableSettings(const std::vector<std::filesystem::path>& legacyDirectories={},bool preferLegacy=false);
// Reuse the same checked temporary-file/atomic-replace writer for typed
// per-model settings. Caller serializes changes to its own settings file.
bool savePortableText(const std::filesystem::path& path,std::string_view text,std::string& error);
bool loadApplicationPreferences(const std::filesystem::path&, ApplicationPreferences&, std::string& error);
bool saveApplicationPreferences(const std::filesystem::path&, const ApplicationPreferences&, std::string& error);
bool loadFanCurveSettings(const std::filesystem::path&, FanCurveTable& cpu, FanCurveTable& gpu, std::string& error);
bool saveFanCurveSettings(const std::filesystem::path&, const FanCurveTable& cpu, const FanCurveTable& gpu, std::string& error);
// Versions 1/2 seed Min from the saved Manual curve and retain Manual itself.
// Default denotes native EC control, not a table of 255 sent to EC.
bool loadFanCurveProfiles(const std::filesystem::path&, FanCurveProfiles&, std::string& error);
// One GUI/CLI conversion to native logical UpT/DownT/PWM points. This only
// prepares data; the existing model controller validates and applies it.
inline CustomFanCurves customCurvesFromFanProfiles(const FanCurveProfiles& profiles){
    CustomFanCurves curves{};
    for(int ch=0;ch<2;++ch){const auto& table=profiles.current(ch);for(int i=0;i<16;++i){
        const int next=i<15?table[i+1].temperature:255;
        curves[ch][i]={table[i].temperature,next==255?255:std::max(0,next-3),table[i].duty};
    }}return curves;
}
bool saveFanCurveProfiles(const std::filesystem::path&, const FanCurveProfiles&, std::string& error);
}
